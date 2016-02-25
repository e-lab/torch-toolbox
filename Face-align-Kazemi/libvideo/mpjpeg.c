// Multipart JPEG library for Torch
// This library allows to receive MP-JPEG images from a server
// and to send them back to a client acting as a server

#define _GNU_SOURCE

#include <luaT.h>
#include <TH/TH.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <setjmp.h>
#ifdef INCLUDEOPENSSL
#include <openssl/ssl.h>
#else
typedef void SSL;
#endif
#include <jpeglib.h>
#include "http.h"

#define RECV_TIMEOUT 600

static char *recvbuf;	// What received from the server
static int c_sk;		// Client socket used to connect to the server
#ifdef INCLUDEOPENSSL
static SSL *c_ssl;		// Client SSL handle
#endif

int mpjpeg_disconnect()
{
#ifdef INCLUDEOPENSSL
	if(c_ssl)
	{
		SSL_shutdown(c_ssl);
		SSL_free(c_ssl);
		c_ssl = 0;
	}
#endif
	if(c_sk)
	{
		close(c_sk);
		c_sk = 0;
	}
	return 0;
}

// Receives from sk or ssl if !=0  and waits at most timeout seconds
static int recvtmout(int sk, SSL *ssl, int timeout, char *buf, int bufsize)
{
	fd_set fs;
	struct timeval tv;

	FD_ZERO(&fs);
	FD_SET(sk, &fs);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	if(select(sk + 1, &fs, 0, 0, &tv) != 1)
		return -1;
#ifdef INCLUDEOPENSSL
	if(ssl)
		return SSL_read(ssl, (char *)buf, bufsize);
#endif
	return recv(sk, (char *)buf, bufsize, 0);
}

// Receives a HTTP header + content from sk/ssl, waits at most timeout, content length (without header) is saved in rlen
static char *httprecv(int sk, SSL *ssl, int timeout, int *rlen)
{
	int bufsize = 3900;
	char *buf = (char *)malloc(bufsize);
	char *p, *q;
	int pcontent, len = 0;
	int rc, contentlength = 0;
	
	bufsize--;
	*buf = 0;
	while(len < bufsize)
	{
		rc = recvtmout(sk, ssl, timeout, buf + len, contentlength ? bufsize - len : 1);
		if(rc <= 0)
		{
			free(buf);
			return 0;
		}
		len += rc;
		buf[len] = 0;
		p = strstr(buf, "\r\n\r\n");
		if(p)
		{
			pcontent = p + 4 - buf;
			if(!contentlength)
			{
				q = strcasestr(buf, "\r\nContent-Length:");
				if(q)
					contentlength = atoi(q + 17);
				bufsize = pcontent + contentlength;
				buf = (char *)realloc(buf, bufsize+1);
			}
			if(!contentlength)
				break;
		}
	}
	*rlen = len;
	return buf;
}

int mpjpeg_connect(const char *url)
{
	char *host, *sendbuf;	// allocated, to be freed before returning
	char *remotefn;
	int rc, usessl = 0;
	struct sockaddr_in addr;
	
	if(c_sk)
		mpjpeg_disconnect();
	if(!memcmp(url, "http://", 7))
		url += 7;
	else if(!memcmp(url, "https://", 8))
	{
#ifdef INCLUDEOPENSSL	
		url += 8;
		if(!ssl_client_ctx)
			return HTTPERR_INITFIRST;
		usessl = 1;
#else
		return HTTPERR_MISSINGTLS;
#endif
	}

	remotefn = strchr(url, '/');
	if(remotefn)
	{
		host = (char *)malloc(remotefn - url + 1);
		memcpy(host, url, remotefn - url);
		host[remotefn - url] = 0;
	} else {
		remotefn = "/";
		host = strdup(url);
	}
	if(!Resolve(host, &addr, usessl ? 443 : 80))
	{
		free(host);
		return HTTPERR_RESOLVE;
	}
	if((c_sk = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		free(host);
		return HTTPERR_SOCKET;
	}
	if(connect(c_sk, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		free(host);
		close(c_sk);
		c_sk = 0;
		return HTTPERR_CONNECTIONREFUSED;
	}
#ifdef INCLUDEOPENSSL
	if(usessl)
	{
		c_ssl = SSL_new(ssl_client_ctx);
		SSL_set_fd(c_ssl, c_sk);
		if(SSL_connect(c_ssl) != 1)
		{
			free(host);
			close(c_sk);
			c_sk = 0;
			SSL_free(c_ssl);
			c_ssl = 0;
			return HTTPERR_COMM;
		}
	}
#endif
	sendbuf = (char *)malloc(300 + strlen(remotefn) + strlen(host));
	sprintf(sendbuf, "GET %s HTTP/1.1\r\n"
		"User-agent: MPJPG library\r\n"
		"Host: %s\r\n"
		"\r\n", remotefn, host);
#ifdef INCLUDEOPENSSL
	if(c_ssl)
		rc = SSL_write(c_ssl, sendbuf, strlen(sendbuf));
	else
#endif
		rc = send(c_sk, sendbuf, strlen(sendbuf), 0);
	free(host);
	if(rc != strlen(sendbuf))
	{
		free(sendbuf);
		return HTTPERR_COMM;
	}
	free(sendbuf);
	return 0;
}

int mpjpeg_getdata(char **data, unsigned *datalen, char *filename, int filename_size)
{
	int recvlen, retry;
	char *p, *q;

	if(!c_sk)
		return HTTPERR_NOTCONNECTED;
	if(recvbuf)
		free(recvbuf);
	for(retry = 0; retry < 5; retry++)
	{
#ifdef INCLUDEOPEN
		recvbuf = httprecv(c_sk, c_ssl, RECV_TIMEOUT, &recvlen);
#else
		recvbuf = httprecv(c_sk, 0, RECV_TIMEOUT, &recvlen);
#endif
		if(!recvbuf)
			break;
		if(recvlen > 0 && (p = strcasestr(recvbuf, "Content-Length:")) &&

			(q = strstr(p, "\r\n\r\n")) )
		{
			q += 4;
			*data = q;
			*datalen = recvlen - (q - recvbuf);
			p = strcasestr(recvbuf, "filename=");
			*filename = 0;
			if(p)
			{
				p += 9;
				q = strchr(p, '\r');
				if(q)
				{
					filename_size--;
					if(q - p < filename_size)
						filename_size = q - p;
					memcpy(filename, p, filename_size);
					filename[filename_size] = 0;
				}
			}
			if(!*filename)
			{
				struct timeval tv;
				struct tm *tm;

				gettimeofday(&tv, 0);
				tm = localtime(&tv.tv_sec);
				snprintf(filename, filename_size, "%04d%02d%02d-%02d%02d%02d-%03d.jpg",
					tm->tm_year + 1900, tm->tm_mon + 1, tm->tm_mday, tm->tm_hour, tm->tm_min, tm->tm_sec, (int)tv.tv_usec / 1000);
			}
			return 0;
		}
	}
	return HTTPERR_COMM;
}


int jpeg_create(const char *path, void *buf, int width, int height, int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *fpo;
	unsigned char *ptr1[32];
	unsigned char **ptr[3] = {ptr1, ptr1+16, ptr1+24};
	int i, j;

	fpo = fopen(path, "w+b");
	if(!fpo)
		return -1;
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	cinfo.in_color_space = JCS_YCbCr;
	cinfo.input_components = 3;
	cinfo.data_precision = 8;
	cinfo.image_width = (JDIMENSION)width;
	cinfo.image_height = (JDIMENSION)height;
	jpeg_set_defaults(&cinfo);
	jpeg_default_colorspace(&cinfo);
	jpeg_set_quality(&cinfo, quality, 75);
	jpeg_stdio_dest(&cinfo, fpo);
	cinfo.raw_data_in = TRUE;
	cinfo.do_fancy_downsampling = FALSE;
	jpeg_start_compress(&cinfo, TRUE);
	for(i = 0; i < height; i += 16)
	{
		for(j = 0; j < 16; j++)
			ptr[0][j] = (unsigned char *)buf + width * (i+j);
		for(j = 0; j < 8; j++)
		{
			ptr[1][j] = (unsigned char *)buf + width*height + width/2 * (i/2+j);
			ptr[2][j] = (unsigned char *)buf + width*height/4*5 + width/2 * (i/2+j);
		}
		jpeg_write_raw_data(&cinfo, ptr, 16);
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	fclose(fpo);
	return 0;
}

int jpeg_create_buf(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	unsigned char *ptr1[32];
	unsigned char **ptr[3] = {ptr1, ptr1+16, ptr1+24};
	int i, j;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	cinfo.in_color_space = JCS_YCbCr;
	cinfo.input_components = 3;
	cinfo.data_precision = 8;
	cinfo.image_width = (JDIMENSION)width;
	cinfo.image_height = (JDIMENSION)height;
	jpeg_set_defaults(&cinfo);
	jpeg_default_colorspace(&cinfo);
	jpeg_set_quality(&cinfo, quality, 75);
	jpeg_mem_dest(&cinfo, dest, destsize);
	cinfo.raw_data_in = TRUE;
	cinfo.do_fancy_downsampling = FALSE;
	jpeg_start_compress(&cinfo, TRUE);
	for(i = 0; i < height; i += 16)
	{
		for(j = 0; j < 16; j++)
			ptr[0][j] = (unsigned char *)buf + width * (i+j);
		for(j = 0; j < 8; j++)
		{
			ptr[1][j] = (unsigned char *)buf + width*height + width/2 * (i/2+j);
			ptr[2][j] = (unsigned char *)buf + width*height/4*5 + width/2 * (i/2+j);
		}
		jpeg_write_raw_data(&cinfo, ptr, 16);
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	return 0;
}

int jpeg_create_buf_422(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality)
{
	struct jpeg_compress_struct cinfo;
	struct jpeg_error_mgr jerr;
	unsigned char *ptr1[32];
	unsigned char **ptr[3] = {ptr1, ptr1+16, ptr1+24};
	int i, j;

	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_compress(&cinfo);
	cinfo.in_color_space = JCS_YCbCr;
	cinfo.input_components = 3;
	cinfo.data_precision = 8;
	cinfo.image_width = (JDIMENSION)width;
	cinfo.image_height = (JDIMENSION)height;
	jpeg_set_defaults(&cinfo);
	jpeg_default_colorspace(&cinfo);
	jpeg_set_quality(&cinfo, quality, 75);
	jpeg_mem_dest(&cinfo, dest, destsize);
	cinfo.raw_data_in = TRUE;
	cinfo.do_fancy_downsampling = FALSE;
	cinfo.comp_info[1].v_samp_factor = cinfo.comp_info[2].v_samp_factor = 2;
	jpeg_start_compress(&cinfo, TRUE);
	for(i = 0; i < height; i += 16)
	{
		for(j = 0; j < 16; j++)
			ptr[0][j] = (unsigned char *)buf + width * (i+j);
		for(j = 0; j < 8; j++)
		{
			ptr[1][j] = (unsigned char *)buf + width*height + width * (i/2+j);
			ptr[2][j] = (unsigned char *)buf + width*height * 3/2 + width * (i/2+j);
		}
		jpeg_write_raw_data(&cinfo, ptr, 16);
	}
	jpeg_finish_compress(&cinfo);
	jpeg_destroy_compress(&cinfo);
	return 0;
}

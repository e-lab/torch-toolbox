// Multipart JPEG library for Torch
// This library allows to receive MP-JPEG images from a server
// and to send them back to a client acting as a server

#define _GNU_SOURCE

#include <luaT.h>
#include <TH/TH.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <string.h>
#include <setjmp.h>
#include <jpeglib.h>
#include <ctype.h>

#define RECV_TIMEOUT 600

static char *recvbuf;	// What received from the server
static int c_sk;		// Client socket used to connect to the server

int mpjpeg_disconnect()
{
	if(c_sk)
	{
		close(c_sk);
		c_sk = 0;
	}
	return 0;
}

// Receives from sk if !=0  and waits at most timeout seconds
static int recvtmout(int sk, int timeout, char *buf, int bufsize)
{
	fd_set fs;
	struct timeval tv;

	FD_ZERO(&fs);
	FD_SET(sk, &fs);
	tv.tv_sec = timeout;
	tv.tv_usec = 0;
	if(select(sk + 1, &fs, 0, 0, &tv) != 1)
		return -1;
	return recv(sk, (char *)buf, bufsize, 0);
}

// Receives a HTTP header + content from sk, waits at most timeout, content length (without header) is saved in rlen
static char *httprecv(int sk, int timeout, int *rlen)
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
		rc = recvtmout(sk, timeout, buf + len, contentlength ? bufsize - len : 1);
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

// Resolve address and convert it to a sockaddr_in structure, complete with port
// address has to be in the form dnsname[:port] or x.x.x.x[:port]
// returns 1 when resolved, 0 when not
int Resolve(const char *address, struct sockaddr_in *addr, int defaultport)
{
	char *p;
	int i;

	memset(addr, 0, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_port = htons((unsigned short)defaultport);
	if( (p = strchr(address, ':')) )
	{
		addr->sin_port = htons((unsigned short)atoi(p + 1));
		*p = 0;
	}
	if(!*address)
		return 0;
	for(i = 0; address[i]; i++)
		if(!isdigit((unsigned char)address[i]) && address[i] != '.')
			break;
	if(address[i])
	{
		struct hostent *h;
		
		h = gethostbyname(address);
		if(!h)
			return 0;	// Error host not found
		addr->sin_addr.s_addr = *((unsigned long *)h->h_addr);
		return 1;
	} else {
		addr->sin_addr.s_addr = inet_addr(address);
		if(addr->sin_addr.s_addr)
			return 1;
		else return 0;
	}
}

int mpjpeg_connect(const char *url)
{
	char *host, *sendbuf;	// allocated, to be freed before returning
	char *remotefn;
	int rc;
	struct sockaddr_in addr;
	
	if(c_sk)
		mpjpeg_disconnect();
	if(!memcmp(url, "http://", 7))
		url += 7;

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
	if(!Resolve(host, &addr, 80))
	{
		free(host);
		return -1;
	}
	if((c_sk = socket(AF_INET, SOCK_STREAM, 0)) < 0)
	{
		free(host);
		return -2;
	}
	if(connect(c_sk, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		free(host);
		close(c_sk);
		c_sk = 0;
		return -3;
	}
	sendbuf = (char *)malloc(300 + strlen(remotefn) + strlen(host));
	sprintf(sendbuf, "GET %s HTTP/1.1\r\n"
		"User-agent: MPJPG library\r\n"
		"Host: %s\r\n"
		"\r\n", remotefn, host);
	rc = send(c_sk, sendbuf, strlen(sendbuf), 0);
	free(host);
	if(rc != strlen(sendbuf))
	{
		free(sendbuf);
		return -4;
	}
	free(sendbuf);
	return 0;
}

int mpjpeg_getdata(char **data, unsigned *datalen, char *filename, int filename_size)
{
	int recvlen, retry;
	char *p, *q;
	long timestamp;

	if(!c_sk)
		return -5;
	if(recvbuf)
		free(recvbuf);
	for(retry = 0; retry < 5; retry++)
	{
		recvbuf = httprecv(c_sk, RECV_TIMEOUT, &recvlen);
		if(!recvbuf)
			break;
		if(recvlen > 0 && (p = strcasestr(recvbuf, "Content-Length:")) &&

			(q = strstr(p, "\r\n\r\n")) )
		{
			q += 4;
			*data = q;
			*datalen = recvlen - (q - recvbuf);
			p = strcasestr(recvbuf, "X-Timestamp:");
			timestamp = p ? strtoul(p+12, 0, 10) : 0;
			if(!timestamp)
			{
				struct timeval tv;
				gettimeofday(&tv, 0);
				timestamp = tv.tv_sec * 1000L + tv.tv_usec / 1000;
			}
			snprintf(filename, filename_size, "%ld", timestamp);
			return 0;
		}
	}
	return -6;
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

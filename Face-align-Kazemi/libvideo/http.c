#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#ifdef INCLUDEOPENSSL
#include <openssl/ssl.h>
SSL_CTX *ssl_client_ctx;
#endif
#include "http.h"

// Initialize OpenSSL
// certfile is a certificate file used to authenticate the server
int https_init(const char *certfile)
{
#ifdef INCLUDEOPENSSL
	SSL_library_init();
	ssl_client_ctx = SSL_CTX_new(TLSv1_client_method());
	
	if(SSL_CTX_load_verify_locations(ssl_client_ctx, certfile ? certfile : 0, certfile ? 0 : "/etc/ssl/certs") != 1)
	{
		SSL_CTX_free(ssl_client_ctx);
		ssl_client_ctx = 0;
		return HTTPERR_CERT;
	}
	SSL_CTX_set_verify(ssl_client_ctx, SSL_VERIFY_PEER, 0);
	return 0;
#endif
	return HTTPERR_MISSINGTLS;
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

// Post the file at path to url, passing destfilename as the file name
// Pass also a username, a password and a device name or id
// If password is absent, basic-auth is used instead
// Returns in retbuf (of retbufsize) the result from the server, complete with the HTTP header
// Returns 0 on success or a negative error code
int postfile(const char *url, const char *path, const char *destfilename, const char *username,
	const char *password, const char *device, char *retbuf, size_t retbufsize)
{
	char host[100], reqbuf[3000], *p, *buf, reqbuf2[500];
	char *proxy;
	int rc, f, sk;
	struct sockaddr_in addr;
	off_t filelen;
#ifdef INCLUDEOPENSSL
	SSL *ssl = 0;
	int usessl = 0;
#endif
	static const char *request = "POST %s HTTP/1.0\r\n"
		"Content-Type: multipart/form-data; boundary=---------------------------7d4ca1220568\r\n"
		"User-Agent: http client\r\n"
		"Host: %s\r\n"
		"Content-Length: %d\r\n"
		"Cache-Control: no-cache\r\n\r\n";
	static const char *request2 = "-----------------------------7d4ca1220568\r\n"
		"Content-Disposition: form-data; name=\"username\"\r\n"
		"\r\n"
		"%s\r\n"
		"-----------------------------7d4ca1220568\r\n"
		"Content-Disposition: form-data; name=\"password\"\r\n"
		"\r\n"
		"%s\r\n"
		"-----------------------------7d4ca1220568\r\n"
		"Content-Disposition: form-data; name=\"datafile\"; filename=\"%s\"\r\n"
		"Content-Type: application/octet-stream\r\n"
		"\r\n";
	static const char *requestend = "\r\n-----------------------------7d4ca1220568--\r\n\r\n";

	static const char *request_basicauth = "POST %s HTTP/1.0\r\n"
		"Content-Type: multipart/form-data; boundary=---------------------------7d4ca1220568\r\n"
		"User-Agent: http client\r\n"
		"Host: %s\r\n"
		"Authorization: Basic %s\r\n"
		"Content-Length: %d\r\n"
		"Cache-Control: no-cache\r\n\r\n";
	static const char *request_basicauth2 = "-----------------------------7d4ca1220568\r\n"
		"Content-Disposition: form-data; name=\"datafile\"; filename=\"%s\"\r\n"
		"Content-Type: application/octet-stream\r\n"
		"\r\n";
	static const char *devicereq = "-----------------------------7d4ca1220568\r\n"
		"Content-Disposition: form-data; name=\"device\"\r\n"
		"\r\n"
		"%s\r\n";

	if(!memcmp(url, "https://", 8))
		proxy = getenv("https_proxy");
	else proxy = getenv("http_proxy");
	if(proxy && *proxy == 0)
		proxy = 0;
	*retbuf = 0;
	f = open(path, O_RDONLY);
	if(f == -1)
		return HTTPERR_FILENOTFOUND;
	filelen = lseek(f, 0, SEEK_END);
	lseek(f, 0, SEEK_SET);
	if(!memcmp(url, "http://", 7))
	{
		if(!proxy)
			url += 7;
	}
	else if(!memcmp(url, "https://", 8))
#ifdef INCLUDEOPENSSL	
	{
		if(!ssl_client_ctx)
			return HTTPERR_INITFIRST;
		if(!proxy)
			url += 8;
		usessl = 1;
	}
#else
		return HTTPERR_MISSINGTLS;
#endif	
	if(proxy)
	{
		const char *url2 = url + 7 + usessl;	// url2 points to address
		
		if(!Resolve(proxy+7+usessl, &addr, usessl ? 443 : 80))
		{
			close(f);
			return HTTPERR_RESOLVE;
		}
		p = strchr(url2, '/');
		if(!p)
			p = (char *)url2 + strlen(url2);
		memcpy(host, url2, p - url2);
		host[p - url2] = 0;
	} else {
		p = strchr(url, '/');
		if(!p)
			p = (char *)url + strlen(url);
		memcpy(host, url, p - url);
		host[p - url] = 0;
		url = p;
		if(!Resolve(host, &addr, usessl ? 443 : 80))
		{
			close(f);
			return HTTPERR_RESOLVE;
		}
	}
	sk = socket(AF_INET, SOCK_STREAM, 0);
	if(sk == -1)
	{
		close(f);
		return HTTPERR_SOCKET;
	}
	if(connect(sk, (struct sockaddr *)&addr, sizeof(addr)))
	{
		close(f);
		close(sk);
		return HTTPERR_CONNECTIONREFUSED;
	}
#ifdef INCLUDEOPENSSL
	if(usessl)
	{
		ssl = SSL_new(ssl_client_ctx);
		SSL_set_fd(ssl, sk);
		int rc;
		if((rc = SSL_connect(ssl)) != 1)
		{
			close(sk);
			close(f);
			SSL_free(ssl);
			return HTTPERR_COMM;
		}
	}
#endif
	if(device && *device)
		sprintf(reqbuf2, devicereq, device);
	else *reqbuf2 = 0;
	if(password && *password)
	{
		sprintf(reqbuf2 + strlen(reqbuf2), request2, username, password, destfilename);
		sprintf(reqbuf, request, url, host, strlen(reqbuf2) + strlen(requestend) + filelen);
	} else {
		sprintf(reqbuf2 + strlen(reqbuf2), request_basicauth2, destfilename);
		sprintf(reqbuf, request_basicauth, url, host, username, strlen(reqbuf2) + strlen(requestend) + filelen);
	}
	strcat(reqbuf, reqbuf2);
#ifdef INCLUDEOPENSSL
	if(usessl)
		rc = SSL_write(ssl, reqbuf, strlen(reqbuf));
	else
#endif
		rc = send(sk, reqbuf, strlen(reqbuf), 0);
	if(rc < 0)
	{
#ifdef INCLUDEOPENSSL
		if(usessl)
			SSL_free(ssl);
#endif
		close(f);
		close(sk);
		return HTTPERR_COMM;
	}
	buf = (char *)malloc(32000);
	while((rc = read(f, buf, 32000)) > 0)
	{
#ifdef INCLUDEOPENSSL
		if(usessl)
			rc = SSL_write(ssl, buf, rc);
		else
#endif
			rc = send(sk, buf, rc, 0);
		if(rc <= 0)
			break;
	}
	free(buf);
	close(f);
	if(rc < 0)
	{
#ifdef INCLUDEOPENSSL
		if(usessl)
			SSL_free(ssl);
#endif
		close(sk);
		return HTTPERR_COMM;
	}
#ifdef INCLUDEOPENSSL
	if(usessl)
		rc = SSL_write(ssl, requestend, strlen(requestend));
	else
#endif
		rc = send(sk, requestend, strlen(requestend), 0);
	if(rc < 0)
	{
#ifdef INCLUDEOPENSSL
		if(usessl)
			SSL_free(ssl);
#endif
		close(sk);
		return HTTPERR_COMM;
	}

	p = retbuf;
	retbufsize--;
	while(p - retbuf < retbufsize)
	{
#ifdef INCLUDEOPENSSL
		if(usessl)
			rc = SSL_read(ssl, p, retbufsize - (p - retbuf));
		else
#endif
			rc = recv(sk, p, retbufsize - (p - retbuf), 0);
		if(rc <= 0)
			break;
		p += rc;
	}
	*p = 0;
#ifdef INCLUDEOPENSSL
	if(usessl)
	{
		SSL_shutdown(ssl);
		SSL_free(ssl);
	}
#endif
	close(sk);
	if(!memcmp(retbuf, "HTTP/1.", 7))
	{
		rc = atoi(retbuf + 8);
		if(rc != 200)
			return rc;
		p = strstr(retbuf, "\r\n\r\n");
		if(p)
		{
			memmove(retbuf, p+4, strlen(p+4) + 1);
			return 0;
		}
	}
	return HTTPERR_PROTOCOL;
}

// Convert an error code to a string
const char *http_error(int rc)
{
	const char *http_errors[] = {
		"No error",
		"File not found",
		"Cannot open socket",
		"Cannot resolve host",
		"Connection refused",
		"Communication error",
		"Missing SSL/TLS support",
		"Cannot load certificate file",
		"Call https_init first",
		"Unexpected HTTP result",
		"Not connected",
		"JPEG decoding error"};
		
	rc = -rc;
	if(rc >= 0 && rc < sizeof(http_errors)/sizeof(http_errors[0]))
		return http_errors[rc];
	return "Unknown error";
}

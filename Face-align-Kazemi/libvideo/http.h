#ifndef _HTTP_H_INCLUDED_
#define _HTTP_H_INCLUDED_

#ifdef INCLUDEOPENSSL
#include <openssl/ssl.h>
#endif

#include <netinet/in.h>
#include <arpa/inet.h>

#define HTTPERR_NOERROR 0
#define HTTPERR_FILENOTFOUND -1
#define HTTPERR_SOCKET -2
#define HTTPERR_RESOLVE -3
#define HTTPERR_CONNECTIONREFUSED -4
#define HTTPERR_COMM -5
#define HTTPERR_MISSINGTLS -6
#define HTTPERR_CERT -7
#define HTTPERR_INITFIRST -8
#define HTTPERR_PROTOCOL -9
#define HTTPERR_NOTCONNECTED -10
#define HTTPERR_DECODE -11

int Resolve(const char *address, struct sockaddr_in *addr, int defaultport);
#ifdef INCLUDEOPENSSL
extern SSL_CTX *ssl_client_ctx;	// Client SSL context
#endif

int postfile(const char *url, const char *path, const char *destfilename, const char *username,
	const char *password, const char *device, char *retbuf, size_t retbufsize);
int https_init(const char *certfile);
const char *http_error(int rc);

// Defined in mpjpeg.c
int mpjpeg_disconnect();
int mpjpeg_connect(const char *url);
int mpjpeg_getdata(char **data, unsigned *datalen, char *filename, int filename_size);
int jpeg_create(const char *path, void *buf, int width, int height, int quality);
int jpeg_create_buf(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality);
int jpeg_create_buf_422(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality);

#endif

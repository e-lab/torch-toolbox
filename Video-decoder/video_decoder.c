/*
 * File:
 *  video_decoder.c
 *
 * Description:
 *  A video decoder that uses avcodec library
 *
 * Author:
 *  Jonghoon Jin // jhjin0@gmail.com
 *  Marko Vitez // marko@vitez.it
 */

#include <luaT.h>
#include <TH/TH.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <libswscale/swscale.h>
#include <pthread.h>
#include <dirent.h>
#include <string.h>
#include <sys/ioctl.h>
#include <net/if.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#ifdef DOVIDEOCAP
#include "videocap.h"
#include "videocodec.h"
#endif

#define BYTE2FLOAT 0.003921568f // 1/255

// Defined in mpjpeg.c
int mpjpeg_disconnect();
int mpjpeg_connect(const char *url);
int mpjpeg_getdata(char **data, unsigned *datalen, char *filename, int filename_size);
int jpeg_create(const char *path, void *buf, int width, int height, int quality);
int jpeg_create_buf(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality);
int jpeg_create_buf_422(unsigned char **dest, unsigned long *destsize, void *buf, int width, int height, int quality);


/* video decoder on DMA memory */
int loglevel = 0;
static int stream_idx;
static AVFormatContext *pFormatCtx;
static AVFormatContext *ofmt_ctx;
static AVCodecContext *pCodecCtx;
static AVFrame *pFrame_yuv;
static int nbuffered_frames;
static AVFrame *pFrame_intm;
static struct SwsContext *sws_ctx;
static uint8_t *sws_rgb;
static uint8_t *lastframe_raw, *jpeg_buf;
static unsigned long jpeg_size;
static int sws_w, sws_h;
static int stream_ended;	// Flag to indicate that we reached the end of the file
static char destfile[500], *destext, destformat[100];
static pthread_t rx_tid;
static int rx_active, frame_decoded;
static pthread_mutex_t readmutex = PTHREAD_MUTEX_INITIALIZER;
static int fragmentsize_seconds;
static int reencode_stream;
static uint64_t start_dts;
static int64_t fragmentsize;
static short audiobuf[16384];
static int audiobuflen;
static int savenow_seconds_before, savenow_seconds_after;
static char savenow_path[300];
#define RXFIFOQUEUESIZE 1000
static AVPacket rxfifo[RXFIFOQUEUESIZE];
static int rxfifo_tail, rxfifo_head;
static int frame_width, frame_height, vcap_fps;
// Encoder variables
static struct {
	int width, height, fps;
	AVFormatContext *fmt_ctx;
	struct SwsContext *sws_ctx;
	uint8_t *sws_rgb;
	AVFrame *pFrame_yuv;
	AVPacket pkt;
	int curframe;
} enc;
// End encoder variables
#ifdef DOVIDEOCAP
static void *vcap, *vcodec, *vcap_frame, *vcodec_extradata;
static int vcodec_writeextradata, vcodec_extradata_size, vcap_nframes;
const int vcodec_gopsize = 12;
#endif

/***************************************
JPEG server stuff
***************************************/

static pthread_mutex_t ntm = PTHREAD_MUTEX_INITIALIZER;	// Protects nclients access
static pthread_rwlock_t rwl = PTHREAD_RWLOCK_INITIALIZER;	// Protects buffers[0] access
static pthread_mutex_t jpegmutex =  PTHREAD_MUTEX_INITIALIZER;	// Protects buffers[1] access
static pthread_mutex_t jpegbufmutex =  PTHREAD_MUTEX_INITIALIZER;	// Protects jpeg_buf
static pthread_cond_t jpegwait = PTHREAD_COND_INITIALIZER;

static int jpegserver_nclients;
#ifdef DOVIDEOCAP
static int jpegseq;
#endif
static struct {
	char *data;
	unsigned datalen;
	unsigned seq;
} buffers[2];
static int srvsk;

#ifdef DARWIN
#define MSG_NOSIGNAL 0
#endif

static void *client_thread(void *sk1)
{
	unsigned lastseq = 0;
	int sk = (int)(size_t)sk1;
	char tmp[3000];
	int rc;
	const char *http200 =
		"HTTP/1.1 200 OK\r\nCache-Control: max-age=0, no-cache, no-store\r\nContent-Type: multipart/x-mixed-replace;boundary=Boundary\r\n\r\n";

	// Ignore HTTP request
	rc = recv(sk, tmp, sizeof(tmp), 0);
	if(rc <= 0)
	{
		close(sk);
		pthread_mutex_lock(&ntm);
		jpegserver_nclients--;
		pthread_mutex_unlock(&ntm);
		return 0;
	}
	send(sk, http200, strlen(http200), MSG_NOSIGNAL);
	for(;;)
	{
		pthread_rwlock_rdlock(&rwl);
		// If there is a new image, send it (keep read-write lock locked in reading)
		if(buffers[0].data && buffers[0].seq != lastseq)
		{
			rc = send(sk, buffers[0].data, buffers[0].datalen, MSG_NOSIGNAL);
			lastseq = buffers[0].seq;
		}
		pthread_rwlock_unlock(&rwl);
		if(rc <= 0)
			break;	// The client has disconnected

		// Check if there is a new image; for this lock both buffers[0] in write mode and buffers[1]
		pthread_rwlock_wrlock(&rwl);
		pthread_mutex_lock(&jpegmutex);
		if(buffers[1].data)
		{
			if(buffers[0].data)
				free(buffers[0].data);
			buffers[0].data = buffers[1].data;
			buffers[0].datalen = buffers[1].datalen;
			buffers[0].seq = buffers[1].seq;
			buffers[1].data = 0;
		}
		pthread_rwlock_unlock(&rwl);
		pthread_cond_wait(&jpegwait, &jpegmutex);
		// We don't need to keep buffers[1] locked
		pthread_mutex_unlock(&jpegmutex);
	}

	pthread_mutex_lock(&ntm);
	jpegserver_nclients--;
	pthread_mutex_unlock(&ntm);
	close(sk);
	return 0;
}

static void *server_thread(void *dummy)
{
	struct sockaddr_in addr;
	socklen_t addrlen;
	int sk;
	pthread_t tid;

	addrlen = sizeof(addr);
	for(;;)
	{
		sk = accept(srvsk, (struct sockaddr *) &addr, &addrlen);
		if(sk < 0)
		{
			usleep(10000);
			continue;
		}
#ifdef DARWIN
		int set = 1;
		setsockopt(sk, SOL_SOCKET, SO_NOSIGPIPE, &set, sizeof(int));
#endif
		pthread_mutex_lock(&ntm);
		jpegserver_nclients++;
		pthread_mutex_unlock(&ntm);
		pthread_create(&tid, 0, client_thread, (void *)(size_t)sk);
		pthread_detach(tid);
	}
	return 0;
}

static int jpegserver_init(lua_State * L)
{
	pthread_t tid;
	struct sockaddr_in addr;
	int yes = 1;

	srvsk = socket(AF_INET, SOCK_STREAM, 0);
	setsockopt(srvsk, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = INADDR_ANY;
	addr.sin_port = htons(lua_tointeger(L, 1));
	if(bind(srvsk, (struct sockaddr *)&addr, sizeof(addr)) < 0)
	{
		close(srvsk);
		srvsk = 0;
		luaL_error(L, "Error binding on port %d", ntohs(addr.sin_port));
	}
	listen(srvsk, 5);
	pthread_create(&tid, 0, server_thread, 0);
	pthread_detach(tid);
	return 0;
}

#ifdef DOVIDEOCAP
static void sendjpeg(const void *buf, unsigned long buflen)
{
	unsigned headerlen;
	const char *httpdata = "--Boundary\r\nContent-Type: image/jpeg\r\nInfo: %s\r\nContent-Length: %d\r\n\r\n";
	const char *info = 0;

	if(!info)
		info = "";
	pthread_mutex_lock(&jpegmutex);
	if(buffers[1].data)
		free(buffers[1].data);
	buffers[1].data = malloc(buflen + strlen(info) + 200);
	sprintf(buffers[1].data, httpdata, info, buflen);
	headerlen = strlen(buffers[1].data);
	memcpy(buffers[1].data + headerlen, buf, buflen);
	buffers[1].data[headerlen + buflen] = '\r';
	buffers[1].data[headerlen + buflen + 1] = '\n';
	buffers[1].datalen = headerlen + buflen + 2;
	buffers[1].seq = ++jpegseq;
	pthread_mutex_unlock(&jpegmutex);
	// Broadcast that there is a new buffers[1]
	pthread_cond_broadcast(&jpegwait);
}
#endif

#define LADDRI ((struct sockaddr_in *)&ifr[i].ifr_addr)->sin_addr.s_addr
#define BADDRI ((unsigned char *)&(((struct sockaddr_in *)&ifr[i].ifr_addr)->sin_addr))

static unsigned long FindLocalHostAddr(char *localhostsaddr, int autolocalhostaddr)
{
	int sk;
	struct ifconf ifc;
	struct ifreq ifr[10];
	int i;
	unsigned localhostaddr = 0;

	sk = socket(AF_INET, SOCK_STREAM, 0);
	ifc.ifc_req = ifr;
	ifc.ifc_len = sizeof ifr;
	if(ioctl(sk, SIOCGIFCONF, &ifc))
	{
		close(sk);
		return 0;
	}
	close(sk);
	if(!autolocalhostaddr && localhostsaddr)
	{
		localhostaddr = inet_addr(localhostsaddr);
		for(i = 0; i * sizeof ifr[0] < (unsigned)ifc.ifc_len; i++)
		{
			if(localhostaddr == LADDRI)
				break;
		}
		if(!LADDRI)
			localhostaddr = 0;
	} else localhostaddr = 0;
	if(!localhostaddr)
	for(i = 0; i * sizeof ifr[0] < (unsigned)ifc.ifc_len; i++)
	{
		if(!localhostaddr || BADDRI[0] != 169)
		{
			localhostaddr = LADDRI;
			if((BADDRI[0] == 192 && BADDRI[1] == 168) ||
				(BADDRI[0] == 10) ||
				(BADDRI[0] == 172 && (BADDRI[1] & 0xf0) == 0x10))
				break;
		}
	}
	if(localhostsaddr)
		strcpy(localhostsaddr, inet_ntoa(*(struct in_addr *)&localhostaddr));
	return localhostaddr;
}

static int getlocalhostaddr(lua_State *L)
{
	char addr[30];

	strcpy(addr, "0.0.0.0");
	FindLocalHostAddr(addr, 1);
	lua_pushstring(L, addr);
	return 1;
}

/***************************************
End of JPEG server stuff
***************************************/

static struct {
	char *data;
	unsigned datalen;
	char filename[101];
} jpeg;

/* yuv420p-to-rgbp lookup table */
static short TB_YUR[256], TB_YUB[256], TB_YUGU[256], TB_YUGV[256], TB_Y[256];
static uint8_t TB_SAT[1024 + 1024 + 256];

/* This function calculates a lookup table for yuv420p-to-rgbp conversion
 * Written by Marko Vitez.
 */
static void video_decoder_yuv420p_rgbp_LUT()
{
	int i;

	/* calculate lookup table for yuv420p */
	for (i = 0; i < 256; i++) {
		TB_YUR[i]  =  459 * (i-128) / 256;
		TB_YUB[i]  =  541 * (i-128) / 256;
		TB_YUGU[i] = -137 * (i-128) / 256;
		TB_YUGV[i] = - 55 * (i-128) / 256;
		TB_Y[i]    = (i-16) * 298 / 256;
	}
	for (i = 0; i < 1024; i++) {
		TB_SAT[i] = 0;
		TB_SAT[i + 1024 + 256] = 255;
	}
	for (i = 0; i < 256; i++)
		TB_SAT[i + 1024] = i;
}

/* This function is a main function for converting color space from yuv420p to planar RGB.
 * It utilizes a lookup table method for fast conversion. Written by Marko Vitez.
 */
static void video_decoder_yuv420p_rgbp(AVFrame * yuv, AVFrame * rgb)
{
	int i, j, U, V, Y, YUR, YUG, YUB;
	int h = yuv->height;
	int w = yuv->width;
	int wy = yuv->linesize[0];
	int wu = yuv->linesize[1];
	int wv = yuv->linesize[2];
	uint8_t *r = rgb->data[0];
	uint8_t *g = rgb->data[1];
	uint8_t *b = rgb->data[2];
	uint8_t *y = yuv->data[0];
	uint8_t *u = yuv->data[1];
	uint8_t *v = yuv->data[2];
	uint8_t *r1, *g1, *b1, *y1;
	w /= 2;
	h /= 2;

	/* convert for R channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			V     = v[j + i * wv];
			YUR   = TB_YUR[V];
			r1    = (uint8_t *) r + 4*w*i + 2*j;

			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024];
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024];
			y1   += wy;
			r1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024];
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024];
		}
	}

	/* convert for G channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			U     = u[j + i * wu];
			V     = v[j + i * wv];
			YUG   = TB_YUGU[U] + TB_YUGV[V];
			g1    = (uint8_t *) g + 4*w*i + 2*j;

			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024];
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024];
			y1   += wy;
			g1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024];
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024];
		}
	}

	/* convert for B channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			U     = u[j + i * wu];
			YUB   = TB_YUB[U];
			b1    = (uint8_t *) b + 4*w*i + 2*j;

			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024];
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024];
			y1   += wy;
			b1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024];
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024];
		}
	}
}

static void video_decoder_rgb_ByteTensor(AVFrame *rgb, uint8_t *dst, long *strides)
{
	int i, j;
	int h = rgb->height;
	int w = rgb->width;

	/* convert for R channel */
	for (i = 0; i < h; i++)
	{
		uint8_t *src = rgb->data[0] + i * rgb->linesize[0];
		uint8_t *r = dst + i * strides[1];
		uint8_t *g = dst + strides[0] + i * strides[1];
		uint8_t *b = dst + 2*strides[0] + i * strides[1];
		for (j = 0; j < w; j++)
		{
			*r++ = *src++;
			*g++ = *src++;
			*b++ = *src++;
		}
	}
}

static void video_decoder_rgb_FloatTensor(AVFrame *rgb, float *dst, long *strides)
{
	int i, j;
	int h = rgb->height;
	int w = rgb->width;

	/* convert for R channel */
	for (i = 0; i < h; i++)
	{
		uint8_t *src = rgb->data[0] + i * rgb->linesize[0];
		float *r = dst + i * strides[1];
		float *g = dst + strides[0] + i * strides[1];
		float *b = dst + 2*strides[0] + i * strides[1];
		for (j = 0; j < w; j++)
		{
			*r++ = BYTE2FLOAT * *src++;
			*g++ = BYTE2FLOAT * *src++;
			*b++ = BYTE2FLOAT * *src++;
		}
	}
}

/* This function is a main function for converting color space from yuv420p to planar RGB
 * directly in torch float tensor
 * It utilizes a lookup table method for fast conversion. Written by Marko Vitez.
 */
static void yuv420p_floatrgbp(AVFrame * yuv, float *dst_float, int imgstride, int rowstride, int w, int h)
{
	int i, j, U, V, Y, YUR, YUG, YUB;
	int wy = yuv->linesize[0];
	int wu = yuv->linesize[1];
	int wv = yuv->linesize[2];
	float *r = dst_float;
	float *g = dst_float + imgstride;
	float *b = dst_float + 2*imgstride;
	uint8_t *y = yuv->data[0];
	uint8_t *u = yuv->data[1];
	uint8_t *v = yuv->data[2];
	uint8_t *y1;
	float *r1, *g1, *b1;
	w /= 2;
	h /= 2;

	/* convert for R channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			V     = v[j + i * wv];
			YUR   = TB_YUR[V];
			r1    = r + 2*(rowstride*i + j);

			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
			y1   += wy;
			r1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
		}
	}

	/* convert for G channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			U     = u[j + i * wu];
			V     = v[j + i * wv];
			YUG   = TB_YUGU[U] + TB_YUGV[V];
			g1    = g + 2*(rowstride*i + j);

			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
			y1   += wy;
			g1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
		}
	}

	/* convert for B channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + 2*i*wy + 2*j;
			U     = u[j + i * wu];
			YUB   = TB_YUB[U];
			b1    = b + 2*(rowstride*i + j);

			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
			y1   += wy;
			b1   += 2*w - 1;
			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
		}
	}
}

/* This function is a main function for converting color space from yuv422p to planar RGB.
 * It utilizes a lookup table method for fast conversion. Written by Marko Vitez.
 */
static void video_decoder_yuv422p_rgbp(AVFrame * yuv, AVFrame * rgb)
{
	int i, j, U, V, Y, YUR, YUG, YUB;
	int h = yuv->height;
	int w = yuv->width;
	int wy = yuv->linesize[0];
	int wu = yuv->linesize[1];
	int wv = yuv->linesize[2];
	uint8_t *r = rgb->data[0];
	uint8_t *g = rgb->data[1];
	uint8_t *b = rgb->data[2];
	uint8_t *y = yuv->data[0];
	uint8_t *u = yuv->data[1];
	uint8_t *v = yuv->data[2];
	uint8_t *r1, *g1, *b1, *y1;
	w /= 2;

	/* convert for R channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			V     = v[j + i * wv];
			YUR   = TB_YUR[V];
			r1    = (uint8_t *) r + 2*(w*i + j);

			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024];
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024];
		}
	}

	/* convert for G channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			U     = u[j + i * wu];
			V     = v[j + i * wv];
			YUG   = TB_YUGU[U] + TB_YUGV[V];
			g1    = (uint8_t *) g + 2*(w*i + j);

			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024];
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024];
		}
	}

	/* convert for B channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			U     = u[j + i * wu];
			YUB   = TB_YUB[U];
			b1    = (uint8_t *) b + 2*(w*i + j);

			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024];
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024];
		}
	}
}

/* This function is a main function for converting color space from yuv422p to planar RGB
 * directly in torch float tensor
 * It utilizes a lookup table method for fast conversion. Written by Marko Vitez.
 */
static void yuv422p_floatrgbp(AVFrame * yuv, float *dst_float, int imgstride, int rowstride, int w, int h)
{
	int i, j, U, V, Y, YUR, YUG, YUB;
	int wy = yuv->linesize[0];
	int wu = yuv->linesize[1];
	int wv = yuv->linesize[2];
	float *r = dst_float;
	float *g = dst_float + imgstride;
	float *b = dst_float + 2*imgstride;
	uint8_t *y = yuv->data[0];
	uint8_t *u = yuv->data[1];
	uint8_t *v = yuv->data[2];
	uint8_t *y1;
	float *r1, *g1, *b1;
	w /= 2;

	/* convert for R channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			V     = v[j + i * wv];
			YUR   = TB_YUR[V];
			r1    = r + i*rowstride + 2*j;

			Y     = TB_Y[y1[0]];
			*r1++ = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*r1   = TB_SAT[Y + YUR + 1024] * BYTE2FLOAT;
		}
	}

	/* convert for G channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			U     = u[j + i * wu];
			V     = v[j + i * wv];
			YUG   = TB_YUGU[U] + TB_YUGV[V];
			g1    = g + i*rowstride + 2*j;

			Y     = TB_Y[y1[0]];
			*g1++ = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*g1   = TB_SAT[Y + YUG + 1024] * BYTE2FLOAT;
		}
	}

	/* convert for B channel */
	for (i = 0; i < h; i++) {
		for (j = 0; j < w; j++) {
			y1    = y + i*wy + 2*j;
			U     = u[j + i * wu];
			YUB   = TB_YUB[U];
			b1    = b + i*rowstride + 2*j;

			Y     = TB_Y[y1[0]];
			*b1++ = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
			Y     = TB_Y[y1[1]];
			*b1   = TB_SAT[Y + YUB + 1024] * BYTE2FLOAT;
		}
	}
}

/* This function is a main function for converting color space from yuv420p to planar YUV.
 * Written by Marko Vitez.
 */
static void video_decoder_yuv420p_yuvp(AVFrame * yuv420, AVFrame * yuv)
{
	int i, j, j2, k;
	int h = yuv420->height;
	int w = yuv420->width;
	int wy = yuv420->linesize[0];
	int wu = yuv420->linesize[1];
	int wv = yuv420->linesize[2];

	uint8_t *srcy = yuv420->data[0];
	uint8_t *srcu = yuv420->data[1];
	uint8_t *srcv = yuv420->data[2];

	uint8_t *dsty = yuv->data[0];
	uint8_t *dstu = yuv->data[1];
	uint8_t *dstv = yuv->data[2];

	uint8_t *dst_y, *dst_u, *dst_v;
	uint8_t *src_y, *src_u, *src_v;

	for (i = 0; i < h; i++) {
		src_y = &srcy[i * wy];
		src_u = &srcu[i * wu / 2];
		src_v = &srcv[i * wv / 2];

		dst_y = &dsty[i * w];
		dst_u = &dstu[i * w];
		dst_v = &dstv[i * w];

		for (j = 0, k = 0; j < w; j++, k += 1) {
			j2 = j >> 1;

			dst_y[k] = src_y[j];
			dst_u[k] = src_u[j2];
			dst_v[k] = src_v[j2];
		}
	}
}

/* This function is a main function for converting color space from YUYV to planar RGB.
 * It can fill directly a torch byte tensor
 * Written by Marko Vitez.
 */
void yuyv2torchRGB(const unsigned char *frame, unsigned char *dst_byte, int imgstride, int rowstride, int w, int h)
{
	int i, j, w2 = w / 2;
	uint8_t *dst;
	const uint8_t *src;

	/* convert for R channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_byte + i * rowstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUR[ src[3] ] + 1024];
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUR[ src[3] ] + 1024];
			src += 4;
		}
	}

	/* convert for G channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_byte + i * rowstride + imgstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUGU[ src[1] ] + TB_YUGV[ src[3] ] + 1024];
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUGU[ src[1] ] + TB_YUGV[ src[3] ] + 1024];
			src += 4;
		}
	}

	/* convert for B channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_byte + i * rowstride + 2*imgstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUB[ src[1] ] + 1024];
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUB[ src[1] ] + 1024];
			src += 4;
		}
	}
}

/* This function is a main function for converting color space from YUYV to planar RGB.
 * It can fill directly a torch float tensor
 * Written by Marko Vitez.
 */
void yuyv2torchfloatRGB(const unsigned char *frame, float *dst_float, int imgstride, int rowstride, int w, int h)
{
	int i, j, w2 = w / 2;
	float *dst;
	const uint8_t *src;

	/* convert for R channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_float + i * rowstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUR[ src[3] ] + 1024] * BYTE2FLOAT;
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUR[ src[3] ] + 1024] * BYTE2FLOAT;
			src += 4;
		}
	}

	/* convert for G channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_float + i * rowstride + imgstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUGU[ src[1] ] + TB_YUGV[ src[3] ] + 1024] * BYTE2FLOAT;
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUGU[ src[1] ] + TB_YUGV[ src[3] ] + 1024] * BYTE2FLOAT;
			src += 4;
		}
	}

	/* convert for B channel */
	src = frame;
	for (i = 0; i < h; i++) {
		dst = dst_float + i * rowstride + 2*imgstride;
		for (j = 0; j < w2; j++) {
			*dst++ = TB_SAT[ TB_Y[ src[0] ] + TB_YUB[ src[1] ] + 1024] * BYTE2FLOAT;
			*dst++ = TB_SAT[ TB_Y[ src[2] ] + TB_YUB[ src[1] ] + 1024] * BYTE2FLOAT;
			src += 4;
		}
	}
}

// Convert packed RGB present in sws_rgb to planar float
void rgb_tofloat(float *dst_float, int imgstride, int linestride)
{
	int c, i, j, srcstride;

	srcstride = (sws_w * 3 + 3) / 4 * 4;
	for(c = 0; c < 3; c++)
		for(i = 0; i < sws_h; i++)
			for(j = 0; j < sws_w; j++)
				dst_float[j + i * linestride + c * imgstride] =
					sws_rgb[c + 3*j + srcstride*i] * BYTE2FLOAT;
}

// Convert planar RGB to packed RGB
void rgb_fromfloat(const float *src_float, int imgstride, int linestride, int width, int height, uint8_t *rgb)
{
	int c, i, j, dststride;

	dststride = (width * 3 + 3) / 4 * 4;
	for(c = 0; c < 3; c++)
		for(i = 0; i < height; i++)
			for(j = 0; j < width; j++)
				rgb[c + 3*j + dststride*i] = src_float[j + i * linestride + c * imgstride] * 255;
}

void rgb_frombyte(const unsigned char *src_float, int imgstride, int linestride, int width, int height, uint8_t *rgb)
{
	int c, i, j, dststride;

	dststride = (width * 3 + 3) / 4 * 4;
	for(c = 0; c < 3; c++)
		for(i = 0; i < height; i++)
			for(j = 0; j < width; j++)
				rgb[c + 3*j + dststride*i] = src_float[j + i * linestride + c * imgstride];
}

void yuyv_toyuv420(const char *from)
{
	int i, j, h = frame_height / 2, w = frame_width / 2;
	uint8_t *lastframe_raw_u = lastframe_raw + frame_width * frame_height;
	uint8_t *lastframe_raw_v = lastframe_raw + frame_width * frame_height / 4 * 5;

	for(i = 0; i < h; i++)
		for(j = 0; j < w; j++)
		{
			lastframe_raw[2*i*frame_width + 2*j] = from[4*i*frame_width + 4*j];
			lastframe_raw[2*i*frame_width + 2*j+1] = from[4*i*frame_width + 4*j+2];
			lastframe_raw[(2*i + 1)*frame_width + 2*j] = from[(2*i+1)*2*frame_width + 4*j];
			lastframe_raw[(2*i + 1)*frame_width + 2*j+1] = from[(2*i+1)*2*frame_width + 4*j+2];
			lastframe_raw_u[i*w + j] = from[4*i*frame_width + 4*j+1];
			lastframe_raw_v[i*w + j] = from[4*i*frame_width + 4*j+3];
		}
}

void scale_torgb(float *dst_float, long *tensor_stride, const char *frame, AVFrame *pFrame_yuv)
{
	// Convert image from YUYV to RGB torch tensor
	const uint8_t *srcslice[3];
	uint8_t *dstslice[3];
	int srcstride[3], dststride[3], height;

#ifdef DOVIDEOCAP
	if(vcap)
	{
		srcslice[0] = (uint8_t *)frame;
		srcslice[1] = srcslice[2] = 0;
		srcstride[0] = 2*frame_width;
		srcstride[1] = srcstride[2] = 0;
		height = frame_height;
		if(!jpegserver_nclients)
			yuyv_toyuv420(frame);
	} else
#endif
	{
		srcslice[0] = pFrame_yuv->data[0];
		srcslice[1] = pFrame_yuv->data[1];
		srcslice[2] = pFrame_yuv->data[2];
		srcstride[0] = pFrame_yuv->linesize[0];
		srcstride[1] = pFrame_yuv->linesize[1];
		srcstride[2] = pFrame_yuv->linesize[2];
		height = pCodecCtx->height;

		// Save frame
		int offs[3], widths[3], stride2[3], i, j;
		offs[0] = 0;
		offs[1] = pCodecCtx->width * pCodecCtx->height;
		offs[2] = pCodecCtx->width * pCodecCtx->height * 5 / 4;
		stride2[0] = pCodecCtx->width;
		stride2[1] = stride2[2] = pCodecCtx->width/2;
		widths[0] = pCodecCtx->width;
		widths[2] = widths[1] = pCodecCtx->width / 2;
		if(lastframe_raw)
		for(i = 0; i < 3; i++)
		{
			int h = pCodecCtx->height;
			if(i > 0)
				h /= 2;
			for(j = 0; j < h; j++)
				memcpy(lastframe_raw + offs[i] + stride2[i]*j, srcslice[i] + srcstride[i]*j, widths[i]);
		}
	}
	dstslice[0] = sws_rgb;
	dstslice[1] = dstslice[2] = 0;
	dststride[0] = (3 * sws_w + 3) / 4 * 4;
	dststride[1] = dststride[2] = 0;
	sws_scale(sws_ctx, srcslice, srcstride, 0, height, dstslice, dststride);
	rgb_tofloat(dst_float, tensor_stride[0], tensor_stride[1]);
}

/*
 * Free and close video decoder
 */
int video_decoder_exit(lua_State * L)
{
	if(rx_tid)
	{
		void *retval;
		rx_active = 0;
		pthread_join(rx_tid, &retval);
		rx_tid = 0;
	}

#ifdef DOVIDEOCAP
	if(vcap)
	{
		videocap_close(vcap);
		vcap = 0;
	}
	if(vcodec)
	{
		videocodec_close(vcodec);
		vcodec = 0;
	}
	if(vcap_frame)
	{
		free(vcap_frame);
		vcap_frame = 0;
	}
	if(vcodec_extradata)
	{
		free(vcodec_extradata);
		vcodec_extradata = 0;
	}
#endif
	/* free the AVFrame structures */
	if (pFrame_intm) {
		av_free(pFrame_intm);
		pFrame_intm = 0;
	}
	if (pFrame_yuv) {
		av_free(pFrame_yuv);
		pFrame_yuv = 0;
	}

	/* close the codec and video file */
	if (pCodecCtx) {
		avcodec_close(pCodecCtx);
		pCodecCtx = 0;
	}
	if (pFormatCtx)
	{
		avformat_close_input(&pFormatCtx);
		pFormatCtx = 0;
	}
	if(ofmt_ctx)
	{
		avformat_free_context(ofmt_ctx);
		ofmt_ctx = 0;
	}

	if(sws_ctx)
	{
		sws_freeContext(sws_ctx);
		sws_ctx = 0;
	}
	if(sws_rgb)
	{
		free(sws_rgb);
		sws_rgb = 0;
	}
	if(lastframe_raw)
	{
		free(lastframe_raw);
		lastframe_raw = 0;
	}
	sws_w = sws_h = 0;
	if(jpeg_buf)
	{
		free(jpeg_buf);
		jpeg_buf = 0;
	}
	frame_decoded = 0;
	stream_ended = 0;
	mpjpeg_disconnect();
	return 0;
}

/* This function initiates libavcodec and its utilities. It finds a valid stream from
 * the given video file and returns the height and width of the input video. The input
 * arguments is the location of file in a string.
 */

static int video_decoder_init(lua_State * L)
{
	int i;
	AVCodec *pCodec;

	video_decoder_exit(NULL);
	/* pass input arguments */
	const char *fpath = lua_tostring(L, 1);
	const char *src_type = lua_tostring(L, 2);
	if(loglevel >= 3)
		fprintf(stderr, "video_decoder_init(%s,%s)\n", fpath, src_type);

	if(src_type && !strcmp(src_type, "MPJPEG"))
	{
		// For JPEGs coming from a webserver with multipart content-type
		// we have our routines
		AVPacket pkt;
		int rc = mpjpeg_connect(fpath);
		if(rc)
			luaL_error(L, "Connection to %s failed: %d", fpath, rc);
		rc = mpjpeg_getdata(&jpeg.data, &jpeg.datalen, jpeg.filename, sizeof(jpeg.filename));
		if(rc)
			luaL_error(L, "Connection to %s failed: %d", fpath, rc);
		// Create a JPEG decoder
		pCodec = avcodec_find_decoder(AV_CODEC_ID_MJPEG);
		if (pCodec == NULL)
			luaL_error(L, "<video_decoder> the codec is not supported");
		pCodecCtx = avcodec_alloc_context3(pCodec);
		if(!pCodecCtx)
			luaL_error(L, "<video_decoder> error allocating codec");
		if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
			video_decoder_exit(NULL);
			luaL_error(L, "<video_decoder> could not open the codec");
		}

		memset(&pkt, 0, sizeof(pkt));
		av_init_packet(&pkt);
		pkt.data = (unsigned char *)jpeg.data;
		pkt.size = jpeg.datalen;
		pkt.flags = AV_PKT_FLAG_KEY;
		pFrame_yuv = avcodec_alloc_frame();
		if(avcodec_decode_video2(pCodecCtx, pFrame_yuv, &i, &pkt) < 0 || !i)
		{
			video_decoder_exit(NULL);
			luaL_error(L, "<video_decoder> Error decoding JPEG image");
		}

		/* allocate an AVFrame structure (No DMA memory) */
		pFrame_intm = avcodec_alloc_frame();
		pFrame_intm->height = pCodecCtx->height;
		pFrame_intm->width = pCodecCtx->width;
		pFrame_intm->data[0] = av_malloc(pCodecCtx->width * pCodecCtx->height);
		pFrame_intm->data[1] = av_malloc(pCodecCtx->width * pCodecCtx->height);
		pFrame_intm->data[2] = av_malloc(pCodecCtx->width * pCodecCtx->height);

		frame_width = pCodecCtx->width;
		frame_height = pCodecCtx->height;
		lua_pushboolean(L, 1);
		lua_pushnumber(L, pCodecCtx->height);
		lua_pushnumber(L, pCodecCtx->width);
		lua_pushnil(L);
		lua_pushnil(L);
		return 5;
	}
	/* use the input format if provided, otherwise guess */
	AVInputFormat *iformat = av_find_input_format(src_type);

	/* open video file */
	if (avformat_open_input(&pFormatCtx, fpath, iformat, NULL) != 0) {
		video_decoder_exit(NULL);
		luaL_error(L, "<video_decoder> no video was provided");
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		video_decoder_exit(NULL);
		luaL_error(L, "<video_decoder> no stream information was found");
	}

	/* dump information about file onto standard error */
	if (loglevel > 0) av_dump_format(pFormatCtx, 0, fpath, 0);

	/* find the first video stream */
	stream_idx = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			stream_idx = i;
			break;
		}
	}
	if (stream_idx == -1) {
		video_decoder_exit(NULL);
		luaL_error(L, "<video_decoder> could not find a video stream");
	}

	/* get a pointer to the codec context for the video stream */
	pCodecCtx = pFormatCtx->streams[stream_idx]->codec;

	/* find the decoder for the video stream */
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		video_decoder_exit(NULL);
		luaL_error(L, "<video_decoder> the codec is not supported");
	}

	/* open codec */
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		video_decoder_exit(NULL);
		luaL_error(L, "<video_decoder> could not open the codec");
	}

	/* allocate a raw AVFrame structure (yuv420p) */
	pFrame_yuv = avcodec_alloc_frame();

	/* allocate an AVFrame structure (No DMA memory) */
	pFrame_intm = avcodec_alloc_frame();
	pFrame_intm->height = pCodecCtx->height;
	pFrame_intm->width = pCodecCtx->width;
	pFrame_intm->data[0] = av_malloc(pCodecCtx->width * pCodecCtx->height);
	pFrame_intm->data[1] = av_malloc(pCodecCtx->width * pCodecCtx->height);
	pFrame_intm->data[2] = av_malloc(pCodecCtx->width * pCodecCtx->height);

    /* calculate fps */
	double frame_rate = pFormatCtx->streams[stream_idx]->avg_frame_rate.num /
		(double) pFormatCtx->streams[stream_idx]->avg_frame_rate.den;

	if(loglevel >= 3)
		fprintf(stderr, "video_decoder_init ok, %dx%d, %ld frames, %f fps\n", pCodecCtx->width,
			pCodecCtx->height, (long)pFormatCtx->streams[i]->nb_frames, frame_rate);
	/* return frame dimensions */
	frame_width = pCodecCtx->width;
	frame_height = pCodecCtx->height;
	lua_pushboolean(L, 1);
	lua_pushnumber(L, pCodecCtx->height);
	lua_pushnumber(L, pCodecCtx->width);
	if (pFormatCtx->streams[stream_idx]->nb_frames > 0) {
		lua_pushnumber(L, pFormatCtx->streams[stream_idx]->nb_frames);
	} else {
		lua_pushnil(L);
	}
	if (frame_rate > 0) {
		lua_pushnumber(L, frame_rate);
	} else {
		lua_pushnil(L);
	}

	return 5;
}

int ToTensor(unsigned char *dst_byte, float *dst_float, long *stride, long *size)
{
	if(dst_byte)
	{
		int c;

		if(pCodecCtx->pix_fmt == PIX_FMT_YUV422P || pCodecCtx->pix_fmt == PIX_FMT_YUVJ422P)
			video_decoder_yuv422p_rgbp(pFrame_yuv, pFrame_intm);
		else if(pCodecCtx->pix_fmt == PIX_FMT_YUV420P || pCodecCtx->pix_fmt == PIX_FMT_YUVJ420P)
			video_decoder_yuv420p_rgbp(pFrame_yuv, pFrame_intm);
		else if(pCodecCtx->pix_fmt == PIX_FMT_RGB24)
			video_decoder_rgb_ByteTensor(pFrame_yuv, dst_byte, stride);
		else return -1;

		/* copy each channel from av_malloc to DMA_malloc */
		if(pCodecCtx->pix_fmt != PIX_FMT_RGB24)
			for (c = 0; c < 3; c++)
				memcpy(dst_byte + c * stride[0],
					   pFrame_intm->data[c],
					   size[1] * size[2]);
	} else {
		if(pCodecCtx->pix_fmt == PIX_FMT_YUV422P || pCodecCtx->pix_fmt == PIX_FMT_YUVJ422P)
			yuv422p_floatrgbp(pFrame_yuv, dst_float, stride[0], stride[1], pCodecCtx->width, pCodecCtx->height);
		else if(pCodecCtx->pix_fmt == PIX_FMT_YUV420P || pCodecCtx->pix_fmt == PIX_FMT_YUVJ420P)
			yuv420p_floatrgbp(pFrame_yuv, dst_float, stride[0], stride[1], pCodecCtx->width, pCodecCtx->height);
		else if(pCodecCtx->pix_fmt == PIX_FMT_RGB24)
			video_decoder_rgb_FloatTensor(pFrame_yuv, dst_float, stride);
		else return -1;
	}
	return 0;
}

/* This function decodes each frame on the fly. Frame is decoded and saved
 * into "pFrame_yuv" as yuv420p, then converted to planar RGB in
 * "pFrame_intm". Finally, memcpy copies all from "pFrame_intm" to
 * dst tensor, which is measured faster than direct writing of planar RGB
 * to dst tensor.
 * This function has been updated to consider two new situations:
 * 1) If vcap!=0, capture has been started by videocap_init, in this case it takes
 *    the frames from the videocap library instead of using libav; videocap support
 *    has to be explicitly enabled by defining DOVIDEOCAP, because it's only present
 *    on Linux; it's supposed that the V4L2 device outputs frames in the YUVV format;
 *    not every webcam supports this format, but it's very common
 * 2) If rx_tid!=0, decoding occurs in another thread (started by startremux), so this
 *    routine only returns the last decoded frame
 */
static int video_decoder_rgb(lua_State * L)
{
	AVPacket packet;
	int dim = 0;
	long *stride = NULL;
	long *size = NULL;
	unsigned char *dst_byte = NULL;
	float *dst_float = NULL;

	const char *tname = luaT_typename(L, 1);
	if (strcmp("torch.ByteTensor", tname) == 0) {
		THByteTensor *frame =
		    luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));

		// get tensor's Info
		dst_byte = THByteTensor_data(frame);
		dim = frame->nDimension;
		stride = &frame->stride[0];
		size = &frame->size[0];

	} else if (strcmp("torch.FloatTensor", tname) == 0) {
		THFloatTensor *frame =
		    luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));

		// get tensor's Info
		dst_float = THFloatTensor_data(frame);
		dim = frame->nDimension;
		stride = &frame->stride[0];
		size = &frame->size[0];

	} else {
		luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	}

	if ((3 != dim) || (3 != size[0])) {
		luaL_error(L, "<video_decoder>: cannot process tensor of this dimension and size");
	}

#ifdef DOVIDEOCAP
	if(vcap)
	{
		if(rx_tid)
		{
			// Wait for the first frame to be decoded
			if(!frame_decoded)
			{
				while(rx_tid && !frame_decoded)
					usleep(10000);
			}
			// pFrame_yuv should not be read while it's being written, so lock a mutex
			pthread_mutex_lock(&readmutex);
			if(!frame_decoded)
			{
				pthread_mutex_unlock(&readmutex);
				lua_pushboolean(L, 0);
				return 1;
			}
			// Convert image from YUYV to RGB torch tensor
			if(dst_byte)
				yuyv2torchRGB((unsigned char *)vcap_frame, dst_byte, stride[0], stride[1], frame_width, frame_height);
			else yuyv2torchfloatRGB((unsigned char *)vcap_frame, dst_float, stride[0], stride[1], frame_width, frame_height);
			pthread_mutex_unlock(&readmutex);
			lua_pushboolean(L, 1);
			return 1;
		}
		char *frame;
		struct timeval tv;

		// Get the frame from the V4L2 device using our videocap library
		int rc = videocap_getframe(vcap, &frame, &tv);
		if(rc < 0)
		{
			luaL_error(L, "videocap_getframe returned error %d", rc);
		}
		// Convert image from YUYV to RGB torch tensor
		if(dst_byte)
			yuyv2torchRGB((unsigned char *)frame, dst_byte, stride[0], stride[1], frame_width, frame_height);
		else yuyv2torchfloatRGB((unsigned char *)frame, dst_float, stride[0], stride[1], frame_width, frame_height);
		lua_pushboolean(L, 1);
		return 1;
	}
#endif
	if(rx_tid)
	{
		// Wait for the first frame to be decoded
		if(!frame_decoded)
		{
			while(rx_tid && !frame_decoded)
				usleep(10000);
		}
		// pFrame_yuv should not be read while it's being written, so lock a mutex
		pthread_mutex_lock(&readmutex);
		if(!frame_decoded)
		{
			pthread_mutex_unlock(&readmutex);
			lua_pushboolean(L, 0);
			return 1;
		}
		// Convert from YUV to RGB
		if(ToTensor(dst_byte, dst_float, stride, size))
			luaL_error(L, "<video_decoder>: unsupported codec pixel format %d", pCodecCtx->pix_fmt);
		pthread_mutex_unlock(&readmutex);

		lua_pushboolean(L, 1);
		return 1;
	}
	for(;;)
	{
		if(!pFormatCtx && !jpeg.data)
			luaL_error(L, "Call init first\n");
		if(!stream_ended)
		{
			int moredata = (pFormatCtx && av_read_frame(pFormatCtx, &packet) >= 0) ||
				(!pFormatCtx && !mpjpeg_getdata(&jpeg.data, &jpeg.datalen, jpeg.filename, sizeof(jpeg.filename)));
			if(!moredata)
				stream_ended = 1;
		}
		if(!pFormatCtx)
		{
			// We are getting data from mpjpeg here, not avformat
			memset(&packet, 0, sizeof(packet));
			av_init_packet(&packet);
			packet.data = (unsigned char *)jpeg.data;
			packet.size = jpeg.datalen;
			packet.flags = AV_PKT_FLAG_KEY;
			packet.stream_index = stream_idx;
			pFrame_yuv = avcodec_alloc_frame();
		}
		/* is this a packet from the video stream? */
		if (stream_ended || packet.stream_index == stream_idx) {

			/* decode video frame */
			if(stream_ended)
			{
				memset(&packet, 0, sizeof(packet));
				packet.stream_index = stream_idx;
			}
			avcodec_decode_video2(pCodecCtx, pFrame_yuv, &frame_decoded, &packet);
			/* check if frame is decoded */
			if (frame_decoded) {

				if(ToTensor(dst_byte, dst_float, stride, size))
					luaL_error(L, "<video_decoder>: unsupported codec pixel format %d", pCodecCtx->pix_fmt);
				if(!stream_ended)
					av_free_packet(&packet);
				lua_pushboolean(L, 1);
				if(!pFormatCtx)
				{
					// MJPEG
					THByteTensor *t = THByteTensor_newWithSize1d(jpeg.datalen);
					unsigned char *data = THByteTensor_data(t);
					memcpy(data, jpeg.data, jpeg.datalen);
					luaT_pushudata(L, t, "torch.ByteTensor");
					lua_pushstring(L, jpeg.filename);
					return 3;
				}
				return 1;
			} else if(stream_ended)
			{
				lua_pushboolean(L, 0);
				return 1;
			}
		}
		/* free the packet that was allocated by av_read_frame */
		av_free_packet(&packet);
	}

	lua_pushboolean(L, 0);
	return 1;
}

int read_next_frame(AVFrame *frame_yuv)
{
	AVPacket packet;

	memset(&packet, 0, sizeof(packet));
	for(;;)
	{
		if(!stream_ended)
		{
			int moredata = (pFormatCtx && av_read_frame(pFormatCtx, &packet) >= 0) ||
				(!pFormatCtx && !mpjpeg_getdata(&jpeg.data, &jpeg.datalen, jpeg.filename, sizeof(jpeg.filename)));
			if(!moredata)
				stream_ended = 1;
		}
		if(!pFormatCtx)
		{
			// We are getting data from mpjpeg here, not avformat
			memset(&packet, 0, sizeof(packet));
			av_init_packet(&packet);
			packet.data = (unsigned char *)jpeg.data;
			packet.size = jpeg.datalen;
			packet.flags = AV_PKT_FLAG_KEY;
			packet.stream_index = stream_idx;
		}
		/* is this a packet from the video stream? */
		if(stream_ended || packet.stream_index == stream_idx)
		{
			/* decode video frame */
			if(stream_ended)
			{
				memset(&packet, 0, sizeof(packet));
				packet.stream_index = stream_idx;
			}
			avcodec_decode_video2(pCodecCtx, frame_yuv, &frame_decoded, &packet);
			av_free_packet(&packet);
			if(frame_decoded)
				return 1;
			else if(stream_ended)
				return 0;
		}
	}
}

void SetRescaler(int w, int h)
{
	if(sws_w == w && sws_h == h)
		return;
	if(sws_ctx)
	{
		sws_freeContext(sws_ctx);
		free(sws_rgb);
	}
	sws_h = h;
	sws_w = w;
#ifdef DOVIDEOCAP
	if(vcap)
		sws_ctx = sws_getContext(frame_width, frame_height, AV_PIX_FMT_YUYV422, sws_w, sws_h, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
	else
#endif
		sws_ctx = sws_getContext(pCodecCtx->width, pCodecCtx->height, pCodecCtx->pix_fmt, sws_w, sws_h, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
	sws_rgb = (uint8_t *)malloc((sws_w * 3 + 3) / 4 * 4 * sws_h + 3);	// +3 because of a bug in sws_scale? it writes more data than it should in (426x240)->(905x510)
}

// This routine resizes the fetched frame
static int video_decoder_resized(lua_State * L)
{
	int dim = 0;
	long *stride = NULL;
	long *size = NULL;
	float *dst_float = NULL;
	THFloatTensor *t;

	const char *tname = luaT_typename(L, 1);
	if(strcmp("torch.FloatTensor", tname))
		luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	t = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
	dst_float = THFloatTensor_data(t);
	dim = t->nDimension;
	stride = &t->stride[0];
	size = &t->size[0];
	if ((3 != dim) || (3 != size[0])) {
		luaL_error(L, "<video_decoder>: cannot process tensor of this dimension and size");
	}

	if(!srvsk && jpeg_buf)
	{
		free(jpeg_buf);
		jpeg_buf = 0;
	}
	SetRescaler(size[2], size[1]);
	if(!lastframe_raw)
		lastframe_raw = (uint8_t *)malloc((pCodecCtx ? pCodecCtx->width * pCodecCtx->height * 3 / 2 : frame_width * frame_height * 2));
#ifdef DOVIDEOCAP
	if(vcap)
	{
		if(rx_tid)
		{
			// Wait for the first frame to be decoded
			if(!frame_decoded)
			{
				while(rx_tid && !frame_decoded)
					usleep(10000);
			}
			// pFrame_yuv should not be read while it's being written, so lock a mutex
			pthread_mutex_lock(&readmutex);
			if(!frame_decoded)
			{
				pthread_mutex_unlock(&readmutex);
				lua_pushboolean(L, 0);
				return 1;
			}
			scale_torgb(dst_float, stride, vcap_frame, 0);
			pthread_mutex_unlock(&readmutex);
			lua_pushboolean(L, 1);
			return 1;
		}
		char *frame;
		struct timeval tv;

		// Get the frame from the V4L2 device using our videocap library
		int rc = videocap_getframe(vcap, &frame, &tv);
		if(rc < 0)
		{
			luaL_error(L, "videocap_getframe returned error %d", rc);
		}
		// Convert image from YUYV to RGB torch tensor
		scale_torgb(dst_float, stride, frame, 0);
		lua_pushboolean(L, 1);
		return 1;
	}
#endif
	if(rx_tid)
	{
		// Wait for the first frame to be decoded
		if(!frame_decoded)
		{
			while(rx_tid && !frame_decoded)
				usleep(10000);
		}
		// pFrame_yuv should not be read while it's being written, so lock a mutex
		pthread_mutex_lock(&readmutex);
		if(!frame_decoded)
		{
			pthread_mutex_unlock(&readmutex);
			lua_pushboolean(L, 0);
			return 1;
		}
		scale_torgb(dst_float, stride, 0, pFrame_yuv);
		pthread_mutex_unlock(&readmutex);

		lua_pushboolean(L, 1);
		return 1;
	}
	if(read_next_frame(pFrame_yuv))
	{
		scale_torgb(dst_float, stride, 0, pFrame_yuv);
		lua_pushboolean(L, 1);
		if(!pFormatCtx)
		{
			// MJPEG
			THByteTensor *t = THByteTensor_newWithSize1d(jpeg.datalen);
			unsigned char *data = THByteTensor_data(t);
			memcpy(data, jpeg.data, jpeg.datalen);
			luaT_pushudata(L, t, "torch.ByteTensor");
			lua_pushstring(L, jpeg.filename);
			return 3;
		}
		return 1;
	}
	lua_pushboolean(L, 0);
	return 1;
}

// This routine takes a batch of frames and resizes them
static int video_decoder_batch_resized(lua_State * L)
{
	int batch = lua_tonumber(L, 1);
	int w = lua_tonumber(L, 2);
	int h = lua_tonumber(L, 3);
	int take = lua_toboolean(L, 4);
	int i;

	if(loglevel >= 5)
		fprintf(stderr, "frame_batch_resized(%d,%d,%d,%d)\n", batch, w, h, take);
	if(batch < 1 || batch > 32)
		luaL_error(L, "batch size can be between 1 and 32");
	THFloatTensor *t;
	if(take)
		t = THFloatTensor_newWithSize4d(batch, 3, h, w);
	else t = THFloatTensor_newWithSize4d(nbuffered_frames, 3, h, w);
	float *dst_float = THFloatTensor_data(t);
	long *stride = &t->stride[0];
	SetRescaler(w, h);
	if(take)
	{
		if(pFrame_yuv)
			av_free(pFrame_yuv);
		pFrame_yuv = av_mallocz(sizeof(AVFrame) * batch);
		for(i = 0; i < batch; i++)
		{
			avcodec_get_frame_defaults(pFrame_yuv + i);
			if(!read_next_frame(pFrame_yuv + i))
				break;
			scale_torgb(dst_float + stride[0] * i, stride+1, 0, pFrame_yuv + i);
		}
	} else {
		for(i = 0; i < nbuffered_frames; i++)
			scale_torgb(dst_float + stride[0] * i, stride+1, 0, pFrame_yuv + i);
	}
	nbuffered_frames = i;
	if(i == 0)
	{
		lua_pushnil(L);
		return 1;
	}
	if(i < batch)
		t = THFloatTensor_newNarrow(t, 0, 0, i);
	luaT_pushudata(L, t, "torch.FloatTensor");
	return 1;
}

// This routine only supports regular libav frames, no vcap, no startremux thread
static int video_decoder_yuv(lua_State * L)
{
	AVPacket packet;
	int c;
	int dim = 0;
	long *stride = NULL;
	long *size = NULL;
	unsigned char *dst_byte = NULL;

	const char *tname = luaT_typename(L, 1);
	if (strcmp("torch.ByteTensor", tname) == 0) {
		THByteTensor *frame =
		    luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));

		// get tensor's Info
		dst_byte = THByteTensor_data(frame);
		dim = frame->nDimension;
		stride = &frame->stride[0];
		size = &frame->size[0];

	} else {
		luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	}

	if ((3 != dim) || (3 != size[0])) {
		luaL_error(L, "<video_decoder>: cannot process tensor of this dimension and size");
	}

	/* read frames and save first five frames to disk */
	while (av_read_frame(pFormatCtx, &packet) >= 0) {

		/* is this a packet from the video stream? */
		if (packet.stream_index == stream_idx) {

			/* decode video frame */
			avcodec_decode_video2(pCodecCtx, pFrame_yuv, &frame_decoded, &packet);

			/* check if frame is decoded */
			if (frame_decoded) {

				/* convert YUV420p to planar YUV */
				video_decoder_yuv420p_yuvp(pFrame_yuv, pFrame_intm);

				/* copy each channel from av_malloc to DMA_malloc */
				for (c = 0; c < dim; c++)
					memcpy(dst_byte + c * stride[0],
					       pFrame_intm->data[c],
					       size[1] * size[2]);

				av_free_packet(&packet);
				lua_pushboolean(L, 1);
				return 1;
			}
		}
		/* free the packet that was allocated by av_read_frame */
		av_free_packet(&packet);
	}

	lua_pushboolean(L, 0);
	return 1;
}

// This routine gets the JPEG of the last got frame; it does not get a new frame!
static int video_decoder_jpeg(lua_State * L)
{
	if(!lastframe_raw)
		return 0;
	if(!jpeg_buf)
		jpeg_create_buf(&jpeg_buf, &jpeg_size, lastframe_raw, frame_width, frame_height, 75);
	THByteTensor *th = THByteTensor_newWithSize1d(jpeg_size);
	memcpy(THByteTensor_data(th), jpeg_buf, jpeg_size);
	luaT_pushudata(L, th, "torch.ByteTensor");
	return 1;
}

// This routine gets the JPEG of the last got frame; it does not get a new frame!
static int save_jpeg(lua_State * L)
{
	const char *filename = lua_tostring(L, 1);
	if(!filename)
		luaL_error(L, "save_jpeg: missing filename");
	if(!lastframe_raw)
	{
		lua_pushboolean(L, 0);
		return 1;
	}
	pthread_mutex_lock(&jpegbufmutex);
	if(!jpeg_buf)
		jpeg_create_buf(&jpeg_buf, &jpeg_size, lastframe_raw, frame_width, frame_height, 75);
	int f = open(filename, O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if(f != -1)
	{
		if(write(f, jpeg_buf, jpeg_size) != jpeg_size && loglevel > 0)
			fprintf(stderr, "Error saving file %s\n", filename);
		close(f);
	} else if(loglevel > 0)
		fprintf(stderr, "Error saving file %s\n", filename);
	pthread_mutex_unlock(&jpegbufmutex);
	lua_pushboolean(L, 1);
	return 1;
}

static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt, const char *tag)
{
	if(loglevel < 7)
		return;
	if(!fmt_ctx)
		fprintf(stderr, "%s stream=%d dur=%d dts=%ld pts=%ld len=%d %s\n", tag, pkt->stream_index, pkt->duration,
			(long)pkt->dts,
			(long)pkt->pts,
			pkt->size, pkt->flags & AV_PKT_FLAG_KEY ? "KEY" : "");
	else fprintf(stderr, "%s stream=%d dur=%d dts=%f pts=%f len=%d %s\n", tag, pkt->stream_index, pkt->duration,
			(double)pkt->dts * fmt_ctx->streams[pkt->stream_index]->time_base.num / fmt_ctx->streams[pkt->stream_index]->time_base.den,
			(double)pkt->pts * fmt_ctx->streams[pkt->stream_index]->time_base.num / fmt_ctx->streams[pkt->stream_index]->time_base.den,
			pkt->size, pkt->flags & AV_PKT_FLAG_KEY ? "KEY" : "");
}

// Open an AVFormatContext for output to destpath with optional format destformat
// Copy most parameters from the already opened input AVFormatContext
static AVFormatContext *openoutput2(lua_State *L, const char *destformat, const char *path, int width, int height, int fps)
{
	AVFormatContext *ofmt_ctx;
	int i, ret;
	struct tm tm;
	time_t t;
	char destpath[300];
	char s[300];

	time(&t);
	tm = *localtime(&t);
	if(path)
		strcpy(destpath, path);
	else if(fragmentsize_seconds != -1)
		sprintf(destpath, "%s_%04d%02d%02d-%02d%02d%02d.%s", destfile, 1900 + tm.tm_year, tm.tm_mon+1, tm.tm_mday,
			tm.tm_hour, tm.tm_min, tm.tm_sec, destext);
	else strcpy(destpath, destfile);
	reencode_stream = -1;
	ofmt_ctx = avformat_alloc_context();
	if(!ofmt_ctx)
	{
		if(L)
			luaL_error(L, "Error allocating format context");
		else fprintf(stderr, "Error allocating format context\n");
		return 0;
	}
	ofmt_ctx->oformat = av_guess_format(destformat, !destformat ? path : 0, 0);
	if(!ofmt_ctx->oformat)
	{
		avformat_free_context(ofmt_ctx);
		if(L)
			luaL_error(L, "Unrecognixed output format %s", destformat);
		else fprintf(stderr, "Unrecognixed output format %s\n", destformat);
		return 0;
	}
	ofmt_ctx->priv_data = NULL;
	// Open the output file
	strncpy(ofmt_ctx->filename, destpath, sizeof(ofmt_ctx->filename));
	if(!width)
		strcat(destpath, ".tmp");
	if(avio_open(&ofmt_ctx->pb, destpath, AVIO_FLAG_WRITE) < 0)
	{
		if(L)
			luaL_error(L, "Error creating output file %s", destpath);
		else fprintf(stderr, "Error creating output file %s\n", destpath);
		avformat_free_context(ofmt_ctx);
		return 0;
	}
	if(pFormatCtx && !width)
	{
		// Copy the stream contexts
		for (i = 0; i < pFormatCtx->nb_streams; i++) {
			AVStream *in_stream, *out_stream;

			in_stream = pFormatCtx->streams[i];
			// If we output in the mp4 container, it's supposed that we will put
			// audio in AAC, so force it
			if(in_stream->codec->codec_type == AVMEDIA_TYPE_AUDIO &&
				in_stream->codec->codec_id != AV_CODEC_ID_AAC
				&& !strcmp(ofmt_ctx->oformat->name, "mp4"))
			{
				AVCodec *decoder;

				out_stream = avformat_new_stream(ofmt_ctx, avcodec_find_encoder(AV_CODEC_ID_AAC));
				out_stream->codec->sample_rate = in_stream->codec->sample_rate;
				out_stream->codec->channels = in_stream->codec->channels;
				out_stream->codec->codec_id = AV_CODEC_ID_AAC;
				out_stream->codec->codec_type = AVMEDIA_TYPE_AUDIO;
				out_stream->codec->frame_size = 1024;
				out_stream->codec->sample_fmt = in_stream->codec->sample_fmt;
				reencode_stream  = i;
				decoder = avcodec_find_decoder(in_stream->codec->codec_id);
				if(decoder == NULL)
				{
					if(L)
						luaL_error(L, "Failed to find audio decoder");
					else fprintf(stderr, "Failed to find audio decoder\n");
					avformat_free_context(ofmt_ctx);
					return 0;
				}
				if(avcodec_open2(in_stream->codec, decoder, 0) < 0)
				{
					if(L)
						luaL_error(L, "Failed to open audio decoder");
					else fprintf(stderr, "Failed to open audio decoder\n");
					avformat_free_context(ofmt_ctx);
					return 0;
				}
				// Required to create the proper stream for the MP4 container
				out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
				if((ret = avcodec_open2(out_stream->codec, 0, 0)) < 0)
				{
					avcodec_close(in_stream->codec);
					av_strerror(ret, s, sizeof(s));
					if(L)
						luaL_error(L, "Failed to open audio encoder: %s", s);
					else fprintf(stderr, "Failed to open audio encoder: %s\n", s);
					avformat_free_context(ofmt_ctx);
					return 0;
				}
			} else {
				out_stream = avformat_new_stream(ofmt_ctx, (AVCodec *)in_stream->codec->codec);
				if (!out_stream) {
					if(L)
						luaL_error(L, "Failed allocating output stream");
					else fprintf(stderr, "Failed allocating output stream\n");
					avformat_free_context(ofmt_ctx);
					return 0;
				}
				ret = avcodec_copy_context(out_stream->codec, in_stream->codec);
				if (ret < 0) {
					if(L)
						luaL_error(L, "Failed to copy context from input to output stream codec context");
					else fprintf(stderr, "Failed to copy context from input to output stream codec context\n");
					avformat_free_context(ofmt_ctx);
					return 0;
				}
				// For MJPEG the aspect ratio is 0/0, fix it
				if(!strcmp(destformat, "avi"))
				{
					out_stream->codec->sample_aspect_ratio.num = 1;
					out_stream->codec->sample_aspect_ratio.den = 1;
				}
				// Copy the aspect ration from the codec to the stream
				out_stream->sample_aspect_ratio = out_stream->codec->sample_aspect_ratio;
				// Take the default codec tag
				out_stream->codec->codec_tag = 0;
			}
			// Use the default time base of 1/90000 seconds used for MPEG-2 TS, it should be good also for other formats
			out_stream->codec->time_base.num = 1;
			out_stream->codec->time_base.den = 90000;
			// If the output fomat requires global hreader, set it
			if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
					out_stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
		}
		// Write the header for the container
		ret = avformat_write_header(ofmt_ctx, NULL);
		if (ret < 0) {
			av_strerror(ret, s, sizeof(s));
			if(L)
				luaL_error(L, "Error writing header: %s", s);
			else fprintf(stderr, "Error writing header: %s\n", s);
			avformat_free_context(ofmt_ctx);
			return 0;
		}
	} else {
		AVCodec *codec;
		AVStream *stream;
		int generic = 0;

		if(!width)
		{
			// vcap case
			width = frame_width;
			height = frame_height;
			fps = vcap_fps;
		} else {
			// generic case
			generic = 1;
		}
#ifdef DOVIDEOCAP
		vcodec_writeextradata = 1;
		if(destformat)
		{
			if(!strcmp(destformat, "mp4"))
				vcodec_writeextradata = 0;
		}
#endif
		enum AVCodecID codec_id;
		if(generic)
		{
			codec_id = av_guess_codec(ofmt_ctx->oformat, destformat, destpath, NULL, AVMEDIA_TYPE_VIDEO);
			codec = avcodec_find_encoder(codec_id);
		} else {
			codec_id = AV_CODEC_ID_H264;
			codec = avcodec_find_decoder(codec_id);
		}
		stream = avformat_new_stream(ofmt_ctx, codec);
		if (ofmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
			stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
		stream->codec->width = width;
		stream->codec->height = height;
		stream->codec->coded_width = width;
		stream->codec->coded_height = height;
		stream->codec->time_base.num = 1;
		stream->codec->time_base.den = generic ? fps : 90000;
		stream->codec->ticks_per_frame = 2;
		stream->codec->pix_fmt = 0;
		stream->time_base.num = 1;
		stream->time_base.den = generic ? fps : 90000;
		stream->avg_frame_rate.num = fps;
		stream->avg_frame_rate.den = 1;
		stream->codec->codec_id = codec_id;
		stream->codec->sample_aspect_ratio.num = 1;
		stream->codec->sample_aspect_ratio.den = 1;
		stream->sample_aspect_ratio = stream->codec->sample_aspect_ratio;
#ifdef DOVIDEOCAP
		stream->codec->extradata = av_malloc(vcodec_extradata_size);
		stream->codec->extradata_size = vcodec_extradata_size;
		memcpy(stream->codec->extradata, vcodec_extradata, vcodec_extradata_size);
#endif
		if(generic)
		{
			if((ret = avcodec_open2(stream->codec, 0, 0)) < 0)
			{
				av_strerror(ret, s, sizeof(s));
				if(L)
					luaL_error(L, "Failed to open video encoder: %s", s);
				else fprintf(stderr, "Failed to open video encoder: %s\n", s);
				avformat_free_context(ofmt_ctx);
				return 0;
			}
		}
		// Write the header for the container
		ret = avformat_write_header(ofmt_ctx, NULL);
		if (ret < 0) {
			av_strerror(ret, s, sizeof(s));
			if(L)
				luaL_error(L, "Error writing header: %s", s);
			else fprintf(stderr, "Error writing header: %s\n", s);
			return 0;
		}
    }
	return ofmt_ctx;
}

static AVFormatContext *openoutput(lua_State *L, const char *destformat, const char *path)
{
	if(ofmt_ctx)
	{
		avformat_free_context(ofmt_ctx);
		ofmt_ctx = 0;
	}
	return openoutput2(L, destformat, path, 0, 0, 0);
}

static void renametmp(const char *path)
{
	char tmp[255];

	strcpy(tmp, path);
	strcat(tmp, ".tmp");
	rename(tmp, path);
}

// Write the input packet pkt to the output stream
static int write_packet(struct AVPacket *pkt, AVRational time_base)
{
	AVStream *out_stream;
	int ret = 0;
	char s[300];
	uint64_t ss;	// start_stream to subtract

	// Calculate the packet parameters for the output stream
	if(pkt->dts == AV_NOPTS_VALUE)
		pkt->dts = 0;
	if(start_dts == -1)
		start_dts = pkt->dts;
	if(pkt->stream_index != reencode_stream)
	{
		if(loglevel >= 5)
			fprintf(stderr, "Write dts=%ld start=%ld size=%ld\n", (long)pkt->dts, (long)start_dts, (long)fragmentsize);
		// If the desired fragment size has been reached and the frame is a key (intra) frame,
		// create a new fragment (we want each fragment to start with a key frame, since
		// inter (non-intra) frames cannot be decoded without a starting key frame
		if(fragmentsize && fragmentsize != -1 &&
			pkt->stream_index == stream_idx && pkt->dts > start_dts + fragmentsize && pkt->flags & AV_PKT_FLAG_KEY)
		{
			if(loglevel >= 4)
				fprintf(stderr, "Close (dts=%ld start=%ld size=%ld)\n", (long)pkt->dts, (long)start_dts, (long)fragmentsize);
			// Write the trailer and close the file
			av_write_trailer(ofmt_ctx);

			/* close output */
			if (ofmt_ctx && !(ofmt_ctx->flags & AVFMT_NOFILE))
			{
				avio_close(ofmt_ctx->pb);
				renametmp(ofmt_ctx->filename);
			}
			avformat_free_context(ofmt_ctx);
			ofmt_ctx = 0;

			// Only if we are continuously creating files, create the new file
			if(fragmentsize_seconds)
			{
				// Open the new file
				ofmt_ctx = openoutput(0, destformat, 0);
				if(!ofmt_ctx)
					return 0;
				start_dts = pkt->dts;
			} else fragmentsize = 0;
		}
		if(ofmt_ctx)
		{
			// Change timing to output stream requirements
			out_stream = ofmt_ctx->streams[pkt->stream_index];
			pkt->duration = av_rescale_q(pkt->duration, time_base, out_stream->time_base);
			if(pFormatCtx)
				ss = av_rescale_q(start_dts, pFormatCtx->streams[stream_idx]->time_base, time_base);
			else ss = start_dts;
			pkt->pts = av_rescale_q(pkt->pts - ss, time_base, out_stream->time_base);
			pkt->dts = av_rescale_q(pkt->dts - ss, time_base, out_stream->time_base);
			pkt->pos = -1;
#ifdef DOVIDEOCAP
			if(vcodec_writeextradata && (pkt->flags & AV_PKT_FLAG_KEY))
			{
				// Streaming formats need extradata to be written periodically
				AVPacket pkt2;

				pkt2 = *pkt;
				pkt->dts++;
				pkt->pts++;
				pkt2.data = vcodec_extradata;
				pkt2.size = vcodec_extradata_size;
				log_packet(ofmt_ctx, &pkt2, "extra");
				ret = av_write_frame(ofmt_ctx, &pkt2);
				if (ret < 0) {
					av_strerror(ret, s, sizeof(s));
					fprintf(stderr, "Error muxing packet: %s\n", s);
				}
			}
#endif
			log_packet(ofmt_ctx, pkt, "out");
			ret = av_write_frame(ofmt_ctx, pkt);
			if (ret < 0) {
				av_strerror(ret, s, sizeof(s));
				fprintf(stderr, "Error muxing packet: %s\n", s);
			}
		}
	} else {
		AVFrame frame;
		int got;

		memset(&frame, 0, sizeof(frame));
		if(avcodec_decode_audio4(pFormatCtx->streams[reencode_stream]->codec, &frame, &got, pkt) >= 0 && got)
		{
			AVPacket pkt2;
			int rc;

			memcpy(audiobuf + audiobuflen, frame.data[0], frame.nb_samples * 2);
			audiobuflen += frame.nb_samples;
			memset(&pkt2, 0, sizeof(pkt2));
			if(audiobuflen >= 1024)
			{
				frame.data[0] = (uint8_t *)audiobuf;
				frame.nb_samples = 1024;
				rc = avcodec_encode_audio2(ofmt_ctx->streams[reencode_stream]->codec, &pkt2, &frame, &got);
				if(!rc && got)
				{
					// Change timing to output stream requirements
					out_stream = ofmt_ctx->streams[pkt->stream_index];
					ss = av_rescale_q(start_dts, pFormatCtx->streams[stream_idx]->time_base, time_base);
					pkt2.dts = pkt2.pts = av_rescale_q(pkt->dts - ss, time_base, out_stream->time_base);
					pkt2.duration = 1024 * 90000 / 8000;
					pkt2.stream_index = pkt->stream_index;
					log_packet(ofmt_ctx, &pkt2, "out");
					// Write the packet
					ret = av_write_frame(ofmt_ctx, &pkt2);
					if (ret < 0) {
						av_strerror(ret, s, sizeof(s));
						fprintf(stderr, "Error muxing packet: %s\n", s);
					}
					av_free_packet(&pkt2);
				}
				audiobuflen -= 1024;
				memmove(audiobuf, audiobuf + 1024, audiobuflen * 2);
			}
		}
	}
	return ret;
}

// Remuxing thread
void *rxthread(void *dummy)
{
	AVPacket pkt;
	int ret = 0;
	char s[300];

	start_dts = -1;
	// Calculate the fragment size in time base units
	audiobuflen = 0;
	if(fragmentsize_seconds == -1)	// Special case, infinite fragment size (streaming)
		fragmentsize = -1;
	else fragmentsize = fragmentsize_seconds * pFormatCtx->streams[stream_idx]->time_base.den /
		pFormatCtx->streams[stream_idx]->time_base.num;

	// While it's allowed to run
    while (rx_active)
	{
		// Read frame
        ret = av_read_frame(pFormatCtx, &pkt);
        if (ret < 0)
            break;

		log_packet(pFormatCtx, &pkt, "in");
		// If video, decode it
		if(pkt.stream_index == stream_idx) {
			/* decode video frame */
			pthread_mutex_lock(&readmutex);
			avcodec_decode_video2(pCodecCtx, pFrame_yuv, &frame_decoded, &pkt);
			pthread_mutex_unlock(&readmutex);
		}
		if(fragmentsize == 0)
		{
			// We are only receiving and not saving, save the received packets in a FIFO buffer
			struct AVPacket pkt2;

			// We have to copy the contents to a new packet, because
			// libav only has a limited amount of packets and after a while
			// they start to cycle and we overrun
			av_new_packet(&pkt2, pkt.size);
			pkt2.stream_index = pkt.stream_index;
			pkt2.dts = pkt.dts;
			pkt2.pts = pkt.pts;
			pkt2.flags = pkt.flags;
			memcpy(pkt2.data, pkt.data, pkt.size);
			rxfifo[rxfifo_tail] = pkt2;
			rxfifo_tail = (rxfifo_tail+1) % RXFIFOQUEUESIZE;
			if(rxfifo_tail == rxfifo_head)
			{
				av_free_packet(&rxfifo[rxfifo_tail]);
				rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
			}
			if((savenow_seconds_before || savenow_seconds_after) && pkt.stream_index == stream_idx)
			{
				int i = rxfifo_tail;
				uint64_t last_dts;

				// Go backward for savenow_seconds_before seconds
				uint64_t sdts = pkt.dts - savenow_seconds_before * pFormatCtx->streams[stream_idx]->time_base.den /
					pFormatCtx->streams[stream_idx]->time_base.num;
				if((int64_t)sdts < 0)
					sdts = 0;
				i = rxfifo_tail;
				start_dts = pkt.dts;
				while(i != rxfifo_head)
				{
					i = (i + RXFIFOQUEUESIZE-1) % RXFIFOQUEUESIZE;
					log_packet(pFormatCtx, &rxfifo[i], "going_back");
					if(rxfifo[i].stream_index == stream_idx && rxfifo[i].dts <= sdts)
						break;
				}
				// Go backward until a keyframe is found
				if(!(rxfifo[i].flags & AV_PKT_FLAG_KEY))
				{
					while(i != rxfifo_head)
					{
						i = (i + RXFIFOQUEUESIZE-1) % RXFIFOQUEUESIZE;
						log_packet(pFormatCtx, &rxfifo[i], "going_back_key");
						if(rxfifo[i].stream_index == stream_idx && rxfifo[i].flags & AV_PKT_FLAG_KEY)
							break;
					}
					// If there is no keyframe, go forward and find first keyframe
					while(i != rxfifo_tail && rxfifo[i].stream_index == stream_idx &&
						!(rxfifo[i].flags & AV_PKT_FLAG_KEY))
					{
						log_packet(pFormatCtx, &rxfifo[i], "going_forward");
						i = (i + 1) % RXFIFOQUEUESIZE;
					}
				}

				// Open the new file
				ofmt_ctx = openoutput(0, destformat, savenow_path);
				if(!ofmt_ctx)
					return 0;
				if(i != rxfifo_tail)
					start_dts = rxfifo[i].dts;
				last_dts = start_dts;

				if(loglevel >= 4)
					fprintf(stderr, "Went back %d seconds, saving %d frames\n", savenow_seconds_before,
						(rxfifo_tail - i + RXFIFOQUEUESIZE) % RXFIFOQUEUESIZE);
				// Write out the buffer
				fragmentsize = 0;
				while(i != rxfifo_tail)
				{
					last_dts = rxfifo[i].dts;
					write_packet(&rxfifo[i], pFormatCtx->streams[rxfifo[i].stream_index]->time_base);
					i = (i + 1) % RXFIFOQUEUESIZE;
				}

				// Clear the FIFO
				while(rxfifo_head != rxfifo_tail)
				{
					av_free_packet(&rxfifo[rxfifo_head]);
					rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
				}
				rxfifo_head = rxfifo_tail = 0;

				// Work done, clear the request
				fragmentsize = savenow_seconds_after * pFormatCtx->streams[stream_idx]->time_base.den /
					pFormatCtx->streams[stream_idx]->time_base.num + (last_dts - start_dts);
				if(loglevel >= 4)
					fprintf(stderr, "Savenow: start_dts = %ld, last_dts = %ld, fragmentsize = %ld\n",
						(long)start_dts, (long)last_dts, (long)fragmentsize);
				savenow_seconds_before = savenow_seconds_after = 0;
			}
			av_free_packet(&pkt);
		} else {
			if(savenow_seconds_after && pkt.stream_index == stream_idx)
			{
				fragmentsize = savenow_seconds_after * pFormatCtx->streams[stream_idx]->time_base.den /
					pFormatCtx->streams[stream_idx]->time_base.num + (pkt.dts - start_dts);
				if(loglevel >= 4)
					fprintf(stderr, "Updating savenow: start_dts = %ld, last_dts = %ld, fragmentsize = %ld\n",
						(long)start_dts, (long)pkt.dts, (long)fragmentsize);
				savenow_seconds_before = savenow_seconds_after = 0;
			}
			write_packet(&pkt, pFormatCtx->streams[pkt.stream_index]->time_base);
			av_free_packet(&pkt);
		}
    }

	if(ofmt_ctx)
	{
		// Write the trailer of the file
		av_write_trailer(ofmt_ctx);

		/* close output */
		if (ofmt_ctx && !(ofmt_ctx->flags & AVFMT_NOFILE))
		{
			avio_close(ofmt_ctx->pb);
			renametmp(ofmt_ctx->filename);
		}
		avformat_free_context(ofmt_ctx);
		ofmt_ctx = 0;

		if (ret < 0 && ret != AVERROR_EOF) {
			av_strerror(ret, s, sizeof(s));
			fprintf(stderr, "Error %d occurred: %s\n", ret, s);
			return 0;
		}
	}

	// Clear the FIFO
	while(rxfifo_head != rxfifo_tail)
	{
		av_free_packet(&rxfifo[rxfifo_head]);
		rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
	}
	rxfifo_head = rxfifo_tail = 0;
    return 0;
}

#ifdef DOVIDEOCAP
// Remuxing thread, vcap case
void *rxthread_vcap(void *dummy)
{
	AVRational time_base;
	int ret = 0;
	char s[300];
	long nframes = 0;

	start_dts = -1;
	time_base.num = 1;
	time_base.den = vcap_fps;
	// Calculate the fragment size in frames
	if(fragmentsize_seconds == -1)	// Special case, infinite fragment size (streaming)
		fragmentsize = -1;
	else fragmentsize = fragmentsize_seconds * vcap_fps;

	// While it's allowed to run
    while (rx_active)
	{
		// Read frame
		char *frame;
		struct timeval tv;
		int keyframe, rc;
		char *outframe;
		unsigned outframelen;
		struct AVPacket pkt;

		// Get the frame from the V4L2 device using our videocap library
		rc = videocap_getframe(vcap, &frame, &tv);
		if(rc < 0)
		{
			fprintf(stderr, "videocap_getframe returned error %d\n", rc);
			break;
		}
		// Save frame for the getframe function
		pthread_mutex_lock(&readmutex);
		memcpy(vcap_frame, frame, frame_width * frame_height * 2);
		frame_decoded = 1;
		pthread_mutex_unlock(&readmutex);
		if(jpegserver_nclients > 0 && lastframe_raw)
		{
			uint8_t *jpeg_buf_tmp = 0;
			unsigned long jpeg_size_tmp = 0;

			yuyv_toyuv420(vcap_frame);
			jpeg_create_buf(&jpeg_buf_tmp, &jpeg_size_tmp, lastframe_raw, frame_width, frame_height, 75);
			pthread_mutex_lock(&jpegbufmutex);
			if(jpeg_buf)
			{
				free(jpeg_buf);
				jpeg_buf = 0;
			}
			jpeg_buf = jpeg_buf_tmp;
			jpeg_size = jpeg_size_tmp;
			pthread_mutex_unlock(&jpegbufmutex);
			sendjpeg(jpeg_buf, jpeg_size);
		}
		if(!vcodec)
			continue;

		// Encode the frame to H.264
		rc = videocodec_process(vcodec, frame, frame_width * frame_height * 2, &outframe, &outframelen, &keyframe);
		// keyframe returned by the encoder is totally wrong,
		// so we force a GOP size of 12 and we know that every 12th frame is a keyframe
		if(rc < 0)
		{
			fprintf(stderr, "videocodec_process returned error %d\n", rc);
			break;
		}
		if(!outframelen)
			continue;
		keyframe = vcap_nframes % vcodec_gopsize == 0;
		vcap_nframes++;

		// Put it in a standard libav packet
		av_new_packet(&pkt, outframelen);
		pkt.stream_index = 0;
		pkt.duration = 1;
		pkt.dts = pkt.pts = nframes++;
		pkt.flags = keyframe ? AV_PKT_FLAG_KEY : 0;
		// We have to copy data, because outframe is a pointer to the driver memory,
		// which is no longer valid after the next videocodec_process
		memcpy(pkt.data, outframe, outframelen);

		log_packet(0, &pkt, "in");

		if(fragmentsize == 0)
		{
			// We are only receiving and not saving, save the received packets in a FIFO buffer
			rxfifo[rxfifo_tail] = pkt;
			rxfifo_tail = (rxfifo_tail+1) % RXFIFOQUEUESIZE;
			if(rxfifo_tail == rxfifo_head)
			{
				av_free_packet(&rxfifo[rxfifo_tail]);
				rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
			}
			if(savenow_seconds_before || savenow_seconds_after)
			{
				int i = rxfifo_tail;
				uint64_t last_dts;

				// Go backward for savenow_seconds_before seconds
				uint64_t sdts = pkt.dts - savenow_seconds_before * vcap_fps;
				if((int64_t)sdts < 0)
					sdts = 0;
				i = rxfifo_tail;
				start_dts = pkt.dts;
				while(i != rxfifo_head)
				{
					i = (i + RXFIFOQUEUESIZE-1) % RXFIFOQUEUESIZE;
					log_packet(0, &rxfifo[i], "going_back");
					if(rxfifo[i].stream_index == stream_idx && rxfifo[i].dts <= sdts)
						break;
				}
				// Go backward until a keyframe is found
				if(!(rxfifo[i].flags & AV_PKT_FLAG_KEY))
				{
					while(i != rxfifo_head)
					{
						i = (i + RXFIFOQUEUESIZE-1) % RXFIFOQUEUESIZE;
						log_packet(0, &rxfifo[i], "going_back_key");
						if(rxfifo[i].stream_index == stream_idx && rxfifo[i].flags & AV_PKT_FLAG_KEY)
							break;
					}
					// If there is no keyframe, go forward and find first keyframe
					while(i != rxfifo_tail && rxfifo[i].stream_index == stream_idx &&
						!(rxfifo[i].flags & AV_PKT_FLAG_KEY))
					{
						log_packet(0, &rxfifo[i], "going_forward");
						i = (i + 1) % RXFIFOQUEUESIZE;
					}
				}

				// Open the new file
				ofmt_ctx = openoutput(0, destformat, savenow_path);
				if(!ofmt_ctx)
					return 0;
				if(i != rxfifo_tail)
					start_dts = rxfifo[i].dts;
				last_dts = start_dts;
				if(loglevel >= 5)
					fprintf(stderr, "openoutput %s last_dts=%ld\n", savenow_path, (long)start_dts);

				if(loglevel >= 4)
					fprintf(stderr, "Went back %d seconds, saving %d frames\n", savenow_seconds_before,
						(rxfifo_tail - i + RXFIFOQUEUESIZE) % RXFIFOQUEUESIZE);

				// Write out the buffer
				fragmentsize = 0;
				while(i != rxfifo_tail)
				{
					last_dts = rxfifo[i].dts;
					write_packet(&rxfifo[i], time_base);
					i = (i + 1) % RXFIFOQUEUESIZE;
				}

				// Clear the FIFO
				while(rxfifo_head != rxfifo_tail)
				{
					av_free_packet(&rxfifo[rxfifo_head]);
					rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
				}
				rxfifo_head = rxfifo_tail = 0;

				// Work done, clear the request
				fragmentsize = savenow_seconds_after * vcap_fps + (last_dts - start_dts);
				if(loglevel >= 4)
					fprintf(stderr, "Savenow: start_dts = %ld, last_dts = %ld, fragmentsize = %ld\n",
						(long)start_dts, (long)last_dts, (long)fragmentsize);
				savenow_seconds_before = savenow_seconds_after = 0;
			}
		} else {
			if(savenow_seconds_after)
			{
				fragmentsize = savenow_seconds_after * vcap_fps + (pkt.dts - start_dts);
				if(loglevel >= 4)
					fprintf(stderr, "Updating savenow: start_dts = %ld, last_dts = %ld, fragmentsize = %ld\n",
						(long)start_dts, (long)pkt.dts, (long)fragmentsize);
				savenow_seconds_before = savenow_seconds_after = 0;
			}
			write_packet(&pkt, time_base);
			av_free_packet(&pkt);
		}
    }

	if(ofmt_ctx)
	{
		// Write the trailer of the file
		av_write_trailer(ofmt_ctx);

		/* close output */
		if (ofmt_ctx && !(ofmt_ctx->flags & AVFMT_NOFILE))
		{
			avio_close(ofmt_ctx->pb);
			renametmp(ofmt_ctx->filename);
		}
		avformat_free_context(ofmt_ctx);
		ofmt_ctx = 0;

		if (ret < 0 && ret != AVERROR_EOF) {
			av_strerror(ret, s, sizeof(s));
			fprintf(stderr, "Error %d occurred: %s\n", ret, s);
			return 0;
		}
	}

	// Clear the FIFO
	while(rxfifo_head != rxfifo_tail)
	{
		av_free_packet(&rxfifo[rxfifo_head]);
		rxfifo_head = (rxfifo_head+1) % RXFIFOQUEUESIZE;
	}
	rxfifo_head = rxfifo_tail = 0;
    return 0;
}
#endif

// Start a thread that reads frames from the input AVFormatContext and remuxes
// them to the specified file

static int startremux(lua_State *L)
{
	if(rx_tid)
	{
		luaL_error(L, "Another startremux already in progress");
	}
#ifdef DOVIDEOCAP
	if(!pFormatCtx && !vcap)
	{
		luaL_error(L, "Call init or capture first");
#else
	if(!pFormatCtx)
	{
		luaL_error(L, "Call init first");
#endif
	}
	strcpy(destfile, lua_tostring(L, 1));
	strcpy(destformat, lua_tostring(L, 2));
	fragmentsize_seconds = lua_tointeger(L, 3);
	if(fragmentsize_seconds != -1)
	{
		destext = strrchr(destfile, '.');
		if(!destext)
			destext = "";
		else *destext++ = 0;
	} else destext = "";
	// Generated files will be in the form destfile_timestamp.extension
	// Create the first fragment and start the decoding thread
	if(fragmentsize_seconds)
	{
		ofmt_ctx = openoutput(L, destformat, 0);
		if(!ofmt_ctx)
		{
			lua_pushboolean(L, 0);
			return 1;
		}
	}
	rx_active = 1;
	savenow_seconds_before = savenow_seconds_after = 0;
#ifdef DOVIDEOCAP
	if(vcap)
	{
		pthread_create(&rx_tid, 0, rxthread_vcap, 0);
		lua_pushboolean(L, 1);
		return 1;
	}
#endif
	pthread_create(&rx_tid, 0, rxthread, 0);
	lua_pushboolean(L, 1);
	return 1;
}

// Stop the remuxing thread
static int stopremux(lua_State *L)
{
	void *retval;

	if(!rx_tid)
	{
		luaL_error(L, "Call startremux first");
	}
	// Tell the thread to stop and wait for it
	rx_active = 0;
	pthread_join(rx_tid, &retval);
	rx_tid = 0;
	lua_pushboolean(L, 1);
	return 1;
}

// Stop the remuxing thread
static int savenow(lua_State *L)
{
	const char *p;

	if(!rx_tid)
	{
		luaL_error(L, "Call startremux first");
	}
	savenow_seconds_before = lua_tointeger(L, 1);
	savenow_seconds_after = lua_tointeger(L, 2);
	p = lua_tostring(L, 3);
	if(!p)
		luaL_error(L, "Missing save path");
	strcpy(savenow_path, p);
	if(loglevel >= 5)
		fprintf(stderr, "savenow(%d,%d,%s)\n", savenow_seconds_before, savenow_seconds_after, savenow_path);
	lua_pushboolean(L, 1);
	return 1;
}

// Set the logging level
static int lua_loglevel(lua_State *L)
{
	loglevel = lua_tointeger(L, 1);
	return 0;
}

static int lua_diffimages(lua_State * L)
{
	int c, x, y;
	int dim = 0;
	long *stride = NULL;
	long *size = NULL;
	unsigned char *img1_byte = NULL, *img2_byte = NULL;
	float *img1_float = NULL, *img2_float = NULL;
	float sens = lua_tonumber(L, 3);
	float area = lua_tonumber(L, 4);
	int diff = 0;

	if(!area)
		area = 0.001;
	const char *tname = luaT_typename(L, 1);
	if (strcmp(luaT_typename(L, 2), tname))
		luaL_error(L, "<video_decoder>: the two tensors need to be of the same type");
	if (strcmp("torch.ByteTensor", tname) == 0) {
		THByteTensor *img1 = luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));
		THByteTensor *img2 = luaT_toudata(L, 2, luaT_typenameid(L, "torch.ByteTensor"));

		// get tensor's Info
		img1_byte = THByteTensor_data(img1);
		img2_byte = THByteTensor_data(img2);
		dim = img1->nDimension;
		stride = &img1->stride[0];
		size = &img1->size[0];
		if(dim != 3)
			luaL_error(L, "<video_decoder>: only 3D tensors are supported as input");
		if(img2->nDimension != 3 || img2->size[0] != size[0] || img2->size[1] != size[1] || img2->size[2] != size[2])
			luaL_error(L, "<video_decoder>: the two images have to be of the same size");
	} else if (strcmp("torch.FloatTensor", tname) == 0) {
		THFloatTensor *img1 = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
		THFloatTensor *img2 = luaT_toudata(L, 2, luaT_typenameid(L, "torch.FloatTensor"));

		// get tensor's Info
		img1_float = THFloatTensor_data(img1);
		img2_float = THFloatTensor_data(img2);
		dim = img1->nDimension;
		stride = &img1->stride[0];
		size = &img1->size[0];
		if(dim != 3)
			luaL_error(L, "<video_decoder>: only 3D tensors are supported as input");
		if(img2->nDimension != 3 || img2->size[0] != size[0] || img2->size[1] != size[1] || img2->size[2] != size[2])
			luaL_error(L, "<video_decoder>: the two images have to be of the same size");
	} else {
		luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	}

	area *= size[0] * size[1] * size[2];
	if(img1_float)
	{
		for(c = 0; c < size[0]; c++)
			for(y = 0; y < size[1]; y++)
				for(x = 0; x < size[2]; x++)
					if(fabs(img1_float[stride[0] * c + stride[1] * y + stride[2] * x] -
						img2_float[stride[0] * c + stride[1] * y + stride[2] * x]) > sens)
							diff++;
	}
	if(img1_byte)
	{
		for(c = 0; c < size[0]; c++)
			for(y = 0; y < size[1]; y++)
				for(x = 0; x < size[2]; x++)
					if(abs(img1_byte[stride[0] * c + stride[1] * y + stride[2] * x] -
						img2_byte[stride[0] * c + stride[1] * y + stride[2] * x]) > sens)
							diff++;
	}
	if(loglevel >= 5)
		fprintf(stderr, "diff=%d (min %d)\n", diff, (int)area);
	lua_pushboolean(L, diff >= area);
	return 1;
}

#ifdef DOVIDEOCAP
// Open the capture device and start it with the given parameters
static int videocap_init(lua_State *L)
{
	const char *device = lua_tostring(L, 1);
	int w = lua_tointeger(L, 2);
	int h = lua_tointeger(L, 3);
	int nbuffers = lua_tointeger(L, 5);
	const char *codec = lua_tostring(L, 6);
	int q = lua_tointeger(L, 7);
	int rc;
	int dummy_keyframe;
	char *extradata;

	if(vcap)
		videocap_close(vcap);
	vcap_nframes = 0;
	vcap = videocap_open(device);
	vcap_fps = lua_tointeger(L, 4);
	if(!q)
		q = 25;
	if(!vcap)
	{
		luaL_error(L, "Error opening device %s", device);
	}
	if(loglevel >= 3)
		fprintf(stderr, "Starting camera capture at %dx%d, fps=%d, nbuffers=%d\n", w, h, vcap_fps, nbuffers ? nbuffers : 1);
	rc = videocap_startcapture(vcap, w, h, V4L2_PIX_FMT_YUYV, vcap_fps, nbuffers ? nbuffers : 1);
	if(rc < 0)
	{
		videocap_close(vcap);
		vcap = 0;
		luaL_error(L, "Error %d starting capture", rc);
	}
	if(codec && *codec)
	{
		vcodec = videocodec_open(codec);
		if(!vcodec)
		{
			videocap_close(vcap);
			vcap = 0;
			luaL_error(L, "Error opening codec device %s", codec);
		}
		rc = videocodec_setcodec(vcodec, V4L2_PIX_FMT_H264);
		// Quantizer, 1-51, lower value means better quality
		// For inter frames, we decrease the quality slightly
		rc = videocodec_setcodecparam(vcodec, V4L2_CID_MPEG_VIDEO_H264_I_FRAME_QP, q);
		rc |= videocodec_setcodecparam(vcodec, V4L2_CID_MPEG_VIDEO_H264_P_FRAME_QP, q+5);
		rc |= videocodec_setcodecparam(vcodec, V4L2_CID_MPEG_VIDEO_H264_B_FRAME_QP, q+5);
		rc |= videocodec_setcodecparam(vcodec, V4L2_CID_MPEG_VIDEO_GOP_SIZE, vcodec_gopsize);
		rc |= videocodec_setcodecparam(vcodec, V4L2_CID_MPEG_VIDEO_H264_LEVEL, V4L2_MPEG_VIDEO_H264_LEVEL_4_0);
		rc |= videocodec_setformat(vcodec, w, h, V4L2_PIX_FMT_YUYV, vcap_fps);
		rc |= videocodec_process(vcodec, (const char *)-1, 0,
			&extradata, (unsigned *)&vcodec_extradata_size, &dummy_keyframe);
		if(rc)
		{
			videocap_close(vcap);
			vcap = 0;
			videocodec_close(vcodec);
			vcodec = 0;
			luaL_error(L, "Error setting encoding parameters");
		}
		vcodec_extradata = malloc(vcodec_extradata_size);
		memcpy(vcodec_extradata, extradata, vcodec_extradata_size);
	}
	vcap_frame = malloc(w * h * 2);
	frame_width = w;
	frame_height = h;
	stream_idx = 0; // Required by write_packet
	lua_pushboolean(L, 1);
	return 1;
}
#endif

struct PD {
	pthread_t tid;
	const char *dirpath, *url, *auth, *device;
};

static int encoderclose(lua_State *L);

static int encoderopen(lua_State *L)
{
	encoderclose(L);
	const char *destformat = lua_tostring(L, 1);
	const char *destpath = lua_tostring(L, 2);
	enc.width = lua_tointeger(L, 3);
	enc.height = lua_tointeger(L, 4);
	enc.fps = lua_tointeger(L, 5);
	enc.curframe = 0;
	enc.fmt_ctx = openoutput2(L, destformat, destpath, enc.width, enc.height, enc.fps);
	enc.sws_ctx = sws_getContext(enc.width, enc.height, AV_PIX_FMT_RGB24, enc.width, enc.height, AV_PIX_FMT_YUV420P, SWS_FAST_BILINEAR, 0, 0, 0);
	enc.sws_rgb = (uint8_t *)malloc((3 * enc.width + 3) / 4 * 4 * enc.height);
	enc.pFrame_yuv = avcodec_alloc_frame();
	enc.pFrame_yuv->height = enc.height;
	enc.pFrame_yuv->width = enc.width;
	enc.pFrame_yuv->data[0] = av_malloc((enc.width + 3) / 4 * 4 * enc.height);
	enc.pFrame_yuv->data[1] = av_malloc((enc.width/2 + 3) / 4 * 4 * (enc.height/2));
	enc.pFrame_yuv->data[2] = av_malloc((enc.width/2 + 3) / 4 * 4 * (enc.height/2));
	enc.pFrame_yuv->linesize[0] = enc.width;
	enc.pFrame_yuv->linesize[1] = enc.width/2;
	enc.pFrame_yuv->linesize[2] = enc.width/2;
	enc.pFrame_yuv->format = AV_PIX_FMT_YUV420P;
	return 0;
}

static int encoderwrite(lua_State *L)
{
	int dim = 0;
	const uint8_t *srcslice[3];
	int srcstride[3];
	int got;

	if(!enc.fmt_ctx)
		luaL_error(L, "<video_decoder>: call encoderopen first");
	const char *tname = luaT_typename(L, 1);
	if (strcmp("torch.ByteTensor", tname) == 0)
	{
		unsigned char *src_byte = NULL;
		THByteTensor *frame = luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));
		src_byte = THByteTensor_data(frame);
		dim = frame->nDimension;
		if((3 != dim) || (3 != frame->size[0]))
			luaL_error(L, "<video_decoder>: cannot process tensor of this dimension and size");
		rgb_frombyte(src_byte, frame->stride[0], frame->stride[1], frame->size[2], frame->size[1], enc.sws_rgb);
	} else if (strcmp("torch.FloatTensor", tname) == 0)
	{
		float *src_float = NULL;
		THFloatTensor *frame = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
		// get tensor's Info
		src_float = THFloatTensor_data(frame);
		dim = frame->nDimension;
		if((3 != dim) || (3 != frame->size[0]))
			luaL_error(L, "<video_decoder>: cannot process tensor of this dimension and size");
		rgb_fromfloat(src_float, frame->stride[0], frame->stride[1], frame->size[2], frame->size[1], enc.sws_rgb);
	} else luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	srcslice[0] = enc.sws_rgb;
	srcslice[1] = srcslice[2] = 0;
	srcstride[0] = (enc.width * 3 + 3) / 4 * 4;
	srcstride[1] = srcstride[2] = 0;
	sws_scale(enc.sws_ctx, srcslice, srcstride, 0, enc.height, enc.pFrame_yuv->data, enc.pFrame_yuv->linesize);
	AVRational invfps = {1, enc.fps};
	enc.pFrame_yuv->pts = av_rescale_q(enc.curframe, invfps, enc.fmt_ctx->streams[0]->time_base);
	enc.curframe++;
	int rc;
	if((rc = avcodec_encode_video2(enc.fmt_ctx->streams[0]->codec, &enc.pkt, enc.pFrame_yuv, &got)) >= 0 && got)
	{
		char s[300];

		int ret = av_write_frame(enc.fmt_ctx, &enc.pkt);
		if (ret < 0)
		{
			av_strerror(ret, s, sizeof(s));
			fprintf(stderr, "Error muxing packet: %s\n", s);
		}
		av_free_packet(&enc.pkt);
	}
	return 0;
}

static int encoderclose(lua_State *L)
{
	if(enc.sws_ctx)
	{
		sws_freeContext(enc.sws_ctx);
		enc.sws_ctx = 0;
	}
	if(enc.sws_rgb)
	{
		free(enc.sws_rgb);
		enc.sws_rgb = 0;
	}
	if(enc.pFrame_yuv)
	{
		av_free(enc.pFrame_yuv->data[0]);
		av_free(enc.pFrame_yuv->data[1]);
		av_free(enc.pFrame_yuv->data[2]);
		avcodec_free_frame(&enc.pFrame_yuv);
	}
	if(enc.fmt_ctx)
	{
		int got;

		while(avcodec_encode_video2(enc.fmt_ctx->streams[0]->codec, &enc.pkt, 0, &got) >= 0 && got)
		{
			int ret = av_write_frame(enc.fmt_ctx, &enc.pkt);
			if (ret < 0)
			{
				char s[300];

				av_strerror(ret, s, sizeof(s));
				fprintf(stderr, "Error muxing packet: %s\n", s);
			}
			av_free_packet(&enc.pkt);
		}
		av_write_trailer(enc.fmt_ctx);
		if (enc.fmt_ctx && !(enc.fmt_ctx->flags & AVFMT_NOFILE))
			avio_close(enc.fmt_ctx->pb);
		avcodec_close(enc.fmt_ctx->streams[0]->codec);
		avformat_free_context(enc.fmt_ctx);
		enc.fmt_ctx = 0;
	}
	return 0;
}

int lua_expandconvresult(lua_State *L)
{
	int j, k, l, ki, k1, k2, li, l1, l2;
	const char *tname = luaT_typename(L, 1);
	if(strcmp("torch.FloatTensor", tname) != 0)
		luaL_error(L, "<video_decoder>: cannot process tensor type %s", tname);
	int expand = lua_tointeger(L, 2);
	THFloatTensor *intens = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
	if(intens->nDimension != 3)
		luaL_error(L, "<video_decoder>: cannot process tensor of dimension %d (only 3)", intens->nDimension);
	float *indata = THFloatTensor_data(intens);
	THFloatTensor *outtens = THFloatTensor_newWithSize3d(intens->size[0], intens->size[1] + expand, intens->size[2] + expand);
	float *outdata = THFloatTensor_data(outtens);
	int istr = intens->stride[1];
	int ostr = outtens->stride[1];
	for(j = 0; j < outtens->size[0]; j++)
	{
		float *ind = indata + intens->stride[0] * j;
		float *outd = outdata + outtens->stride[0] * j;
		for(k = 0; k < outtens->size[1]; k++)
		{
			k1 = k < expand ? 0 : k - expand;
			k2 = k >= intens->size[1] ? intens->size[1] - 1 : k;
			for(l = 0; l < outtens->size[2]; l++)
			{
				l1 = l < expand ? 0 : l - expand;
				l2 = l >= intens->size[2] ? intens->size[2] - 1 : l;
				float max = -1e38;
				for(ki = k1; ki <= k2; ki++)
					for(li = l1; li <= l2; li++)
						if(ind[ki * istr + li] > max)
							max = ind[ki * istr + li];
				outd[k * ostr + l] = max;
			}
		}
	}
	luaT_pushudata(L, outtens, "torch.FloatTensor");
	return 1;
}

/* Availabe functions:

init(file to open, optional path), returns
    status (1=ok, 0=failed)
	width
	height
	number of present frames
	frame rate

	Opens a file/stream with libavformat

capture(device_path, width, height[, fps[, nbuffers[, encoder_path, encoder_quality]]]), returns
    status (1=ok, 0=failed)

	Opens a video capture device with the videocap library
	This function is only available on Linux
	device_path is in the form /dev/videoN
	fps can be 0 (default)
	default number of buffers is 1
	encoder path is the path of the encoder device
	encoder_quality is the quality of the generated stream (suggested:20-30)
	These two optional parameters are necessary if startremux will be used

frame_rgb(tensor), returns
	status (1=ok, 0=failed)

	Gets the next frame in RGB format from the file/stream/device
	tensor has to be torch.ByteTensor or torch.FloatTensor and have dimension 3
	and the first size has to be 3

frame_yuv(tensor), returns
	status (1=ok, 0=failed)

	Gets the next frame in YUV format from the file/stream
	tensor has to be torch.ByteTensor and have dimension 3 and the first size has to be 3

frame_resized(tensor), returns
	status (1=ok, 0=failed)

	Gets the next frame in RGB format from the file/stream/device
	tensor has to be torch.FloatTensor and have dimension 3
	and the first size has to be 3. The image is resized to the tensor size
	and before being resized, is saved to a temporary buffer for subsequent JPEG encoding

frame_batch_resized(batch, width, height, take), returns
	4D image tensor

	If take is true, gets the next batch frames in RGB format from the file or stream,
	otherwise the images are taken from the internal buffer
	Rescale them to width x height and return them in a 4D (batch, 3, height, width) tensor
	batch can be max 32 because frames are buffered by libavcodec, which has 32 buffers

frame_jpeg(), returns
	status (true=ok, false=nothing to encode (frame_resized never called))
	byte tensor containing the JPEG image or nil

	Encodes in JPEG the frame previously received with frame_resized
	It will not be encoded again (the cached copy will be used) if frame_jpeg() or
	save_jpeg() has been already called for that frame

save_jpeg(filename), returns
	status (true=ok, false=nothing to encode (frame_resized never called))

	Encodes in JPEG the frame previously received with frame_resized and saves it to filename
	It will not be encoded again (the cached copy will be used) if frame_jpeg() or
	save_jpeg() has been already called for that frame

exit()
	Stops and closes the decoder/video capture device/receiving thread

startremux(fragment_base_path, format, fragment_size), returns
	status (1=ok, 0=failed)

	Starts to receive from the stream opened with init and starts to
	write file fragments of size fragment_size seconds (0=don't save
	anything, only save after a savenow command, -1=don't fragment,
	generate a continuous stream, use this when streaming)
	fragment_base_path in the form A.B is changed to A_timestamp.B
	format is the file format (optional), if it cannot be deduced
	from the file extension

savenow(seconds before, seconds after, destfilename), returns
	status (1=ok, 0=failed)

	Saves the buffered frames from at least now - <seconds before> to
	now + <seconds after>. Receiving should have been started with
	startremux before giving this command. <seconds before> is an "at least"
	value, because saving always starts with a keyframe; of course, it's
	less than this if there is not enough buffered data; destfilename
	is the name of the file that will be saved, both locally and eventually
	on the remote side

stopremux(), returns
	status (1=ok, 0=failed)

loglevel(level), no return value

	Sets the logging level (0=no logging)

diffimages(tensor1, tensor2, sensitivity, area), return bool

	Calculates if there was a significant change between the two images
	The difference of at least area (given as a fraction of total area) pixels
	have to different, where a pixel is considered different when the absolute
	difference between the two is higher of sensitivity.

encoderopen(format,path,width,height,fps)

	Opens the encoder, tested formats are "mp4", "avi" and "mpeg"

encoderwrite(frame)

	Sends a frame to the encoder

encoderclose()

	Closes the encoder

expandconvresult(tensor, expand), returns expanded_tensor

	Expands the input 3D tensor to another 3D tensor, where the second and third
	dimensions are higher for the given expand amount. The input tensor represents
	squares of (expand+1) x (expand+1) size in the second and third dimensions
	in an array of size (size(2) + expand) x (size(3) + expand), so the maximums
	of those squares are taken

*/


static const struct luaL_reg video_decoder[] = {
	{"init", video_decoder_init},
#ifdef DOVIDEOCAP
	{"capture", videocap_init},
#endif
	{"frame_rgb", video_decoder_rgb},
	{"frame_yuv", video_decoder_yuv},
	{"frame_resized", video_decoder_resized},
	{"frame_batch_resized", video_decoder_batch_resized},
	{"frame_jpeg", video_decoder_jpeg},
	{"save_jpeg", save_jpeg},
	{"exit", video_decoder_exit},
	{"startremux", startremux},
	{"stopremux", stopremux},
	{"savenow", savenow},
	{"loglevel", lua_loglevel},
	{"diffimages", lua_diffimages},
	{"jpegserver_init", jpegserver_init},
	{"localhostaddr", getlocalhostaddr},
	{"encoderopen", encoderopen},
	{"encoderwrite", encoderwrite},
	{"encoderclose", encoderclose},
	{"expandconvresult", lua_expandconvresult},
	{NULL, NULL}
};

// Initialize the library
int luaopen_libvideo_decoder(lua_State * L)
{
	luaL_register(L, "libvideo_decoder", video_decoder);
	/* pre-calculate lookup table */
	video_decoder_yuv420p_rgbp_LUT();
	/* register libav */
	av_register_all();
	avformat_network_init();
	return 1;
}

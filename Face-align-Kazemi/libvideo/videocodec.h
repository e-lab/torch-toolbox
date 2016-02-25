#ifndef _VIDEOCODEC_H_INCLUDED_
#define _VIDEOCODEC_H_INCLUDED_

#include <linux/videodev2.h>

#define VIDEOCODEC_ERR_OK 0
#define VIDEOCODEC_ERR_SET_FORMAT -1
#define VIDEOCODEC_ERR_REQBUFS -2
#define VIDEOCODEC_ERR_MALLOC -3
#define VIDEOCODEC_ERR_ENQUEUE_BUFFERS -4
#define VIDEOCODEC_ERR_DEQUEUE_BUFFERS -5
#define VIDEOCODEC_ERR_START_STREAMING -6
#define VIDEOCODEC_ERR_QUERY -7
#define VIDEOCODEC_ERR_QUERYBUF -8
#define VIDEOCODEC_ERR_MMAP -9
#define VIDEOCODEC_ERR_SET_FRAMERATE -10
#define VIDEOCODEC_ERR_SET_CODEC -11
#define VIDEOCODEC_ERR_AVFORMAT -12
#define VIDEOCODEC_ERR_BADURL -13
#define VIDEOCODEC_ERR_WRITE -14
#define VIDEOCODEC_ERR_NOTINIT -15
#define VIDEOCODEC_ERR_FINISHED -16

void *videocodec_open(const char *devname);
void *videocodec_opendecoder(const char *devname);
int videocodec_capabilities(void *v, struct v4l2_capability *cap);
int videocodec_setformat(void *v, int w, int h, unsigned format, int fps);
int videocodec_setcodec(void *v, int codec);
int videocodec_setcodecparam(void *v, int id, int value);
int videocodec_start(void *v);
int videocodec_process(void *v, const char *inframe, unsigned inframelen, char **outframe, unsigned *outframelen, int *keyframe);
int videocodec_close(void *v);
int videocodec_createfile(void *v, const char *url, const char *format);
int videocodec_writefile(void *v, const char *inframe, unsigned inframelen);
int videocodec_writeencfile(void *v, const char *inframe, unsigned inframelen);
#endif

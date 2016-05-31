#ifndef _VIDEOCAP_H_INCLUDED_
#define _VIDEOCAP_H_INCLUDED_

#include <linux/videodev2.h>

#define VIDEOCAP_ERR_OK 0
#define VIDEOCAP_ERR_SET_FORMAT -1
#define VIDEOCAP_ERR_REQBUFS -2
#define VIDEOCAP_ERR_MALLOC -3
#define VIDEOCAP_ERR_ENQUEUE_BUFFERS -4
#define VIDEOCAP_ERR_DEQUEUE_BUFFERS -5
#define VIDEOCAP_ERR_START_STREAMING -6
#define VIDEOCAP_ERR_QUERY -7
#define VIDEOCAP_ERR_QUERYBUF -8
#define VIDEOCAP_ERR_MMAP -9
#define VIDEOCAP_ERR_SET_FRAMERATE -10
#define VIDEOCAP_ERR_ENUM -12

// Open the video device (normally /dev/videoN) and return a handle (0 if open was unsuccessful)
void *videocap_open(const char *devname);
// Return the capabilities of the opened device (check V4L2 documentation for the description of v4l2_capability)
int videocap_capabilities(void *v, struct v4l2_capability *cap);
// Return the available frame rates for the given resolution and format; index can range from 0 until an error is returned
// format is one of the V4L2_FMT_ constants
int videocap_framerates(void *v, int index, int w, int h, unsigned format, double *fps);
// Return the available formats; index can range from 0 until an error is returned
int videocap_formats(void *v, int index, char *desc, unsigned *pixelformat);
// Start capture at the specified resolution, format, fps and number of buffers. If fps is zero, the default will be used
int videocap_startcapture(void *v, int w, int h, unsigned format, int fps, int nbuffers);
// Wait and return a pointer to a captured frame; tv contains the time of the capture
int videocap_getframe(void *v, char **frame, struct timeval *tv);
// Stop every activity and close the video capture device
int videocap_close(void *v);

#endif

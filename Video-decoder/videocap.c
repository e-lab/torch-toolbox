#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include "videocap.h"

#define N_CAPTURE_BUFFERS 16

typedef struct {
	int fd;
	unsigned w, h, framesize;
	unsigned format;
	unsigned curbufidx;
	unsigned nbuffers;
	struct v4l2_buffer buffers[N_CAPTURE_BUFFERS];
	void *pointers[N_CAPTURE_BUFFERS];
} VIDEOCAP;

void *videocap_open(const char *devname)
{
	int fd = open(devname, O_RDWR, 0);
	if(fd == -1)
		return 0;
	VIDEOCAP *v = (VIDEOCAP *)calloc(1, sizeof(VIDEOCAP));
	v->fd = fd;
	return v;
}

int videocap_capabilities(void *v, struct v4l2_capability *cap)
{
	if(ioctl(((VIDEOCAP *)v)->fd, VIDIOC_QUERYCAP, cap) == -1)
		return VIDEOCAP_ERR_QUERY;
	return 0;
}

int videocap_framerates(void *v, int index, int w, int h, unsigned format, double *fps)
{
	struct v4l2_frmivalenum arg;
	VIDEOCAP *v1 = (VIDEOCAP *)v;
	arg.index = index;
	arg.pixel_format = format;
	arg.width = w;
	arg.height = h;
	if(ioctl(v1->fd, VIDIOC_ENUM_FRAMEINTERVALS, &arg) == -1)
		return VIDEOCAP_ERR_ENUM;
	*fps = (double)arg.discrete.denominator / arg.discrete.numerator;
	return 0;
}

int videocap_formats(void *v, int index, char *desc, unsigned *pixelformat)
{
	struct v4l2_fmtdesc arg;
	VIDEOCAP *v1 = (VIDEOCAP *)v;
	arg.index = index;
	arg.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(v1->fd, VIDIOC_ENUM_FMT, &arg) == -1)
		return VIDEOCAP_ERR_ENUM;
	strcpy(desc, (const char *)arg.description);
	*pixelformat = arg.pixelformat;
	return 0;
}

static int set_format(VIDEOCAP *v, int w, int h, unsigned format)
{
	struct v4l2_format fmt;
	
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fmt.fmt.pix.width = w;
	fmt.fmt.pix.height = h;
	fmt.fmt.pix.pixelformat = format;
	if(ioctl(v->fd, VIDIOC_S_FMT, &fmt) == -1)
		return -1;
	v->framesize = w * h * 2;
	v->w = w;
	v->h = h;
	v->format = format;
	return 0;
}

static int set_userpointers(VIDEOCAP *v)
{
	struct v4l2_requestbuffers reqbuf;

	memset(&reqbuf, 0, sizeof (reqbuf));
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_USERPTR;

	if(ioctl(v->fd, VIDIOC_REQBUFS, &reqbuf) == -1)
		return -1;
	return 0;
}

static int free_buffers(VIDEOCAP *v)
{
	int i;
	
	for(i = 0; i < v->nbuffers; i++)
		if(v->pointers[i])
		{
			if(v->buffers[i].memory == V4L2_MEMORY_MMAP)
				munmap(v->pointers[i], v->buffers[i].length);
			else {
				free((void *)v->pointers[i]);
				v->pointers[i] = 0;
			}
		}
	return 0;
}

static int create_mmap_buffers(VIDEOCAP *v)
{
	struct v4l2_requestbuffers reqbuf;
	unsigned i;

	memset(&reqbuf, 0, sizeof (reqbuf));
	reqbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.count = v->nbuffers;

	if(ioctl(v->fd, VIDIOC_REQBUFS, &reqbuf) == -1)
		return VIDEOCAP_ERR_REQBUFS;
	v->nbuffers = reqbuf.count;
	for(i = 0; i <  reqbuf.count; i++)
	{
		memset(&v->buffers[i], 0, sizeof(v->buffers[i]));
		v->buffers[i].index = i;
		v->buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v->buffers[i].memory = V4L2_MEMORY_MMAP;
		if(ioctl(v->fd, VIDIOC_QUERYBUF, &v->buffers[i]) == -1)
		{
			free_buffers(v);
			return VIDEOCAP_ERR_QUERYBUF;
		}
		v->pointers[i] = mmap(NULL, v->buffers[i].length, PROT_READ | PROT_WRITE,
				MAP_SHARED, v->fd, v->buffers[i].m.offset);
		if(v->pointers[i] == (void *)-1)
		{
			v->pointers[i] = 0;
			free_buffers(v);
			return VIDEOCAP_ERR_MMAP;
		}
	}
	return 0;	
}

static int create_buffers(VIDEOCAP *v)
{
	int i;
	
	for(i = 0; i < v->nbuffers; i++)
	{
		memset(&v->buffers[i], 0, sizeof(v->buffers[i]));
		v->buffers[i].index = i;
		v->buffers[i].type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
		v->buffers[i].memory = V4L2_MEMORY_USERPTR;
		v->pointers[i] = malloc(v->framesize);
		v->buffers[i].m.userptr = (unsigned long)v->pointers[i];
		if(v->buffers[i].m.userptr == 0)
		{
			free_buffers(v);
			return -1;
		}
	}
	return 0;
}

static int enqueue_buffers(VIDEOCAP *v)
{
	int i;
	
	for(i = 0; i < v->nbuffers-1; i++)
		if(ioctl(v->fd, VIDIOC_QBUF, &v->buffers[i]) == -1)
			return -1;
	return 0;
}

static int start_streaming(VIDEOCAP *v)
{
	int buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(v->fd, VIDIOC_STREAMON, &buf_type))
		return -1;
	v->curbufidx = 0;
	return 0;
}

int stop_streaming(VIDEOCAP *v)
{
	int buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	if(ioctl(v->fd, VIDIOC_STREAMOFF, &buf_type))
		return -1;
	return 0;
}

static int set_framerate(VIDEOCAP *v, int rate)
{
	struct v4l2_streamparm fps;

	memset(&fps, 0, sizeof(fps));
	fps.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
	fps.parm.capture.timeperframe.numerator = 1;
	fps.parm.capture.timeperframe.denominator = rate;
	if(ioctl(v->fd, VIDIOC_S_PARM, &fps) == -1)
		return -1;
	return 0;
}

int videocap_startcapture(void *v, int w, int h, unsigned format, int fps, int nbuffers)
{
	int rc;
	VIDEOCAP *v1 = (VIDEOCAP *)v;
	if(nbuffers < 1 || nbuffers > N_CAPTURE_BUFFERS)
		return VIDEOCAP_ERR_REQBUFS;
	v1->nbuffers = nbuffers;
	if(set_format(v1, w, h, format))
		return VIDEOCAP_ERR_SET_FORMAT;
	if(fps)
	{
		if(set_framerate(v1, fps))
			return VIDEOCAP_ERR_SET_FRAMERATE;
	}
	rc = create_mmap_buffers(v1);
	if(rc)
	{
		if(set_userpointers(v1))
			return rc;
		if(create_buffers(v1))
			return VIDEOCAP_ERR_MALLOC;
	}
	if(enqueue_buffers(v1))
	{
		free_buffers(v1);
		return VIDEOCAP_ERR_ENQUEUE_BUFFERS;
	}
	if(start_streaming(v))
	{
		free_buffers(v1);
		return VIDEOCAP_ERR_START_STREAMING;
	}
	return 0;
}

static int enqueue_buffer(VIDEOCAP *v, int idx)
{
	if(ioctl(v->fd, VIDIOC_QBUF, &v->buffers[idx]) == -1)
		return -1;
	return 0;
}

static int dequeue_buffer(VIDEOCAP *v, int idx)
{
	if(ioctl(v->fd, VIDIOC_DQBUF, &v->buffers[idx]) == -1)
		return -1;
	return 0;
}

int videocap_getframe(void *v, char **frame, struct timeval *tv)
{
	VIDEOCAP *v1 = (VIDEOCAP *)v;
	
	if(enqueue_buffer(v1, (v1->curbufidx + v1->nbuffers - 1) % v1->nbuffers))
		return VIDEOCAP_ERR_ENQUEUE_BUFFERS;
	if(dequeue_buffer(v1, v1->curbufidx))
		return VIDEOCAP_ERR_DEQUEUE_BUFFERS;
	*frame = (char *)v1->pointers[v1->curbufidx];
	*tv = v1->buffers[v1->curbufidx].timestamp;
	v1->curbufidx = (v1->curbufidx + 1) % v1->nbuffers;
	return 0;
}

int videocap_close(void *v)
{
	VIDEOCAP *v1 = (VIDEOCAP *)v;
	stop_streaming(v1);
	free_buffers(v1);
	close(v1->fd);
	free(v1);
	return 0;
}

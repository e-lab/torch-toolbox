#include <fcntl.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include "videocodec.h"

#define MAX_BUFFERS 32
#define N_BUFFERS 4
#define MAX_STREAM_SIZE (1*1024*1024)

static int avformat_init;

typedef struct {
	int fd, started, ended, init, avformat_header_written;
	int duration, writeextradata;
	char decoder;	// This is a decoder
	unsigned w, h, framerate;
	unsigned format;
	unsigned nbuffers[2];	// 0 = in, 1 = out
	unsigned curbufidx[2];
	unsigned ninframes, count, noutframes;
	unsigned bytesperline[2], sizeimage[2];
	struct v4l2_buffer buffers[2][MAX_BUFFERS];
	struct v4l2_plane planes[2][MAX_BUFFERS][2];
	void *pointers[2][MAX_BUFFERS][2];
	AVFormatContext *fmt_ctx;
} VIDEOCODEC;

void *videocodec_open(const char *devname)
{
	int fd,  i, j;
	char devname2[20];
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmt;
	
	if(devname)
	{
		if(!strcmp(devname, "auto"))
		{		
			for(i = 0; i < 32; i++)
			{
				sprintf(devname2, "/dev/video%d", i);
				fd = open(devname2, O_RDWR, 0);
				if(fd != -1)
				{					
					if(!ioctl(fd, VIDIOC_QUERYCAP, &cap) &&
						(!strcmp((char *)cap.driver, "s5p-mfc") || !strcmp((char *)cap.driver, "MFC")))
					{
						for(j = 0;; j++)
						{
							fmt.index = j;
							fmt.type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
							if(ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == -1)
								break;
							if(fmt.pixelformat == V4L2_PIX_FMT_NV12M)
							{
								i = 32;
								devname = devname2;
								break;
							}
						}
					}
					close(fd);
				}
			}
		}
		fd = open(devname, O_RDWR, 0);
		if(fd == -1)
			return 0;
		VIDEOCODEC *v = (VIDEOCODEC *)calloc(1, sizeof(VIDEOCODEC));
		v->fd = fd;
		return v;
	} else return calloc(1, sizeof(VIDEOCODEC));
}

void *videocodec_opendecoder(const char *devname)
{
	int fd,  i, j;
	char devname2[20];
	struct v4l2_capability cap;
	struct v4l2_fmtdesc fmt;
	
	if(devname)
	{
		if(!strcmp(devname, "auto"))
		{		
			for(i = 0; i < 32; i++)
			{
				sprintf(devname2, "/dev/video%d", i);
				fd = open(devname2, O_RDWR, 0);
				if(fd != -1)
				{					
					if(!ioctl(fd, VIDIOC_QUERYCAP, &cap) &&
						(!strcmp((char *)cap.driver, "s5p-mfc") || !strcmp((char *)cap.driver, "MFC")))
					{
						for(j = 0;; j++)
						{
							fmt.index = j;
							fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
							if(ioctl(fd, VIDIOC_ENUM_FMT, &fmt) == -1)
								break;
							if(fmt.pixelformat == V4L2_PIX_FMT_NV12M)
							{
								i = 32;
								devname = devname2;
								break;
							}
						}
					}
					close(fd);
				}
			}
		}
		fd = open(devname, O_RDWR, 0);
		if(fd == -1)
			return 0;
		VIDEOCODEC *v = (VIDEOCODEC *)calloc(1, sizeof(VIDEOCODEC));
		v->fd = fd;
		v->decoder = 1;
		return v;
	} else {
		VIDEOCODEC *v = (VIDEOCODEC *)calloc(1, sizeof(VIDEOCODEC));
		v->decoder = 1;
		return v;
	}
}

int videocodec_capabilities(void *v, struct v4l2_capability *cap)
{
	if(ioctl(((VIDEOCODEC *)v)->fd, VIDIOC_QUERYCAP, cap) == -1)
		return VIDEOCODEC_ERR_QUERY;
	return 0;
}

static int set_framerate(VIDEOCODEC *v, int rate)
{
	struct v4l2_streamparm fps;

	v->framerate = rate;
	if(v->fd)
	{
		memset(&fps, 0, sizeof(fps));
		fps.type = v->decoder ?  V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fps.parm.output.timeperframe.numerator = 1000;
		fps.parm.output.timeperframe.denominator = rate;

		if(ioctl(v->fd, VIDIOC_S_PARM, &fps) == -1)
			return -1;
	}
	return 0;
}

static int enum_formats(VIDEOCODEC *v, int out)
{
	struct v4l2_fmtdesc fmt;
	int i;

	printf("%s formats:\n", out ? "Output" : "Input");
	for(i = 0;; i++)
	{
		fmt.index = i;
		fmt.type = out ? V4L2_BUF_TYPE_VIDEO_CAPTURE : V4L2_BUF_TYPE_VIDEO_OUTPUT;
		if(ioctl(v->fd, VIDIOC_ENUM_FMT, &fmt) == -1)
			break;
		printf("%d: %x %s %c%c%c%c\n", i, fmt.flags, (char *)fmt.description,
			fmt.pixelformat & 0xff, fmt.pixelformat>>8 & 0xff, fmt.pixelformat>>16 & 0xff, fmt.pixelformat>>24 & 0xff);
	}
	return 0;
}

static int set_format(VIDEOCODEC *v, int w, int h, int format)
{
	struct v4l2_format fmt;
	int i;

	if(v->fd)
	{
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = v->decoder ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
		fmt.fmt.pix_mp.width = w;
		fmt.fmt.pix_mp.height = h;
		fmt.fmt.pix_mp.pixelformat = V4L2_PIX_FMT_NV12M;
		fmt.fmt.pix_mp.num_planes = 2;
		if(ioctl(v->fd, VIDIOC_S_FMT, &fmt) == -1)
			return -1;
		if(ioctl(v->fd, VIDIOC_G_FMT, &fmt) == -1)
			return -1;
		for(i = 0; i < 2; i++)
		{
			v->bytesperline[i] = fmt.fmt.pix_mp.plane_fmt[i].bytesperline;
			v->sizeimage[i] = fmt.fmt.pix_mp.plane_fmt[i].sizeimage;
		}
	}
	v->w = w;
	v->h = h;
	v->format = format;
	return 0;
}

static int set_codec(VIDEOCODEC *v, int codec)
{
	struct v4l2_format fmt;

	enum_formats(v, 0);
	enum_formats(v, 1);
	memset(&fmt, 0, sizeof(fmt));
	fmt.type = v->decoder ? V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE : V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	fmt.fmt.pix_mp.pixelformat = codec;
	fmt.fmt.pix_mp.plane_fmt[0].sizeimage = MAX_STREAM_SIZE;
	fmt.fmt.pix_mp.num_planes = 1;

	if(ioctl(v->fd, VIDIOC_S_FMT, &fmt) == -1)
		return -1;

	return 0;
}

static int free_buffers(VIDEOCODEC *v)
{
	int dir, i, j;
	
	for(dir = 0; dir < 2; dir++)
		for(i = 0; i < MAX_BUFFERS; i++)
			for(j = 0; j < 2; j++)
				if(v->pointers[dir][i][j])
					munmap(v->pointers[dir][i][j], v->buffers[dir][i].m.planes[j].length);
	return 0;
}

static int get_required_buffers(VIDEOCODEC *v, int dir)
{
	struct v4l2_control ctrl;

	if(!v->decoder || dir == 0)
		return N_BUFFERS;
	ctrl.id = V4L2_CID_MIN_BUFFERS_FOR_CAPTURE;
	if(ioctl(v->fd, VIDIOC_G_CTRL, &ctrl) == -1)
		return -1;
	if(ctrl.value + N_BUFFERS > MAX_BUFFERS)
		return -1;
	return ctrl.value + N_BUFFERS;
}

static int create_mmap_buffers(VIDEOCODEC *v, int dir)
{
	struct v4l2_requestbuffers reqbuf;
	unsigned i, j;
	int buftype = dir ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;

	memset(&reqbuf, 0, sizeof (reqbuf));
	reqbuf.type = buftype;
	reqbuf.memory = V4L2_MEMORY_MMAP;
	reqbuf.count = get_required_buffers(v, dir);
	if(reqbuf.count < 0)
		return VIDEOCODEC_ERR_REQBUFS;

	if(ioctl(v->fd, VIDIOC_REQBUFS, &reqbuf) == -1)
		return VIDEOCODEC_ERR_REQBUFS;
	v->nbuffers[dir] = reqbuf.count;
	for(i = 0; i < reqbuf.count; i++)
	{
		memset(&v->buffers[dir][i], 0, sizeof(v->buffers[dir][i]));
		v->buffers[dir][i].index = i;
		v->buffers[dir][i].type = buftype;
		v->buffers[dir][i].memory = V4L2_MEMORY_MMAP;
		v->buffers[dir][i].m.planes = v->planes[dir][i];
		v->buffers[dir][i].length = 2;
		if(ioctl(v->fd, VIDIOC_QUERYBUF, &v->buffers[dir][i]) == -1)
		{
			free_buffers(v);
			return VIDEOCODEC_ERR_QUERYBUF;
		}
		for(j = 0; j < v->buffers[dir][i].length; j++)	// planes
		{
			if(dir == 0 && !v->decoder)
				v->buffers[dir][i].m.planes[j].bytesused = v->buffers[dir][i].m.planes[j].length;
			v->pointers[dir][i][j] = mmap(NULL, v->buffers[dir][i].m.planes[j].length, PROT_READ | PROT_WRITE,
					MAP_SHARED, v->fd, v->buffers[dir][i].m.planes[j].m.mem_offset);
			if(v->pointers[dir][i][j] == (void *)-1)
			{
				v->pointers[dir][i][j] = 0;
				free_buffers(v);
				return VIDEOCODEC_ERR_MMAP;
			}
		}
	}
	return 0;	
}

static int enqueue_buffers(VIDEOCODEC *v, int dir)
{
	int i;
	
	for(i = 0; i < v->nbuffers[dir] - 1; i++)
		if(ioctl(v->fd, VIDIOC_QBUF, &v->buffers[dir][i]) == -1)
			return -1;
	return 0;
}

static int start_streaming(VIDEOCODEC *v, int dir)
{
	int buf_type = dir ? V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE : V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if(ioctl(v->fd, VIDIOC_STREAMON, &buf_type))
		return -1;
	return 0;
}

static int stop_streaming(VIDEOCODEC *v)
{
	int buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
	if(ioctl(v->fd, VIDIOC_STREAMOFF, &buf_type))
		return -1;
	buf_type = V4L2_BUF_TYPE_VIDEO_OUTPUT_MPLANE;
	if(ioctl(v->fd, VIDIOC_STREAMOFF, &buf_type))
		return -1;
	return 0;
}

int videocodec_setformat(void *v, int w, int h, unsigned format, int fps)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	if(set_format(v1, w, h, format))
		return VIDEOCODEC_ERR_SET_FORMAT;
	if(fps && set_framerate(v1, fps))
		return 0;//VIDEOCODEC_ERR_SET_FRAMERATE; // On the XU3 it always fails, so just ignore
	return 0;
}

static int get_format(VIDEOCODEC *v)
{
	struct v4l2_format fmt;

	if(v->fd)
	{
		memset(&fmt, 0, sizeof(fmt));
		fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
		if(ioctl(v->fd, VIDIOC_G_FMT, &fmt) == -1)
			return -1;
		v->w = fmt.fmt.pix_mp.width;
		v->h = fmt.fmt.pix_mp.height;
		v->bytesperline[0] = fmt.fmt.pix_mp.plane_fmt[0].bytesperline;
		v->bytesperline[1] = fmt.fmt.pix_mp.plane_fmt[1].bytesperline;
	}
	return 0;
}

static int prepare_buffers(VIDEOCODEC *v, int dir)
{
	int rc;
	
	rc = create_mmap_buffers(v, dir);
	if(rc)
		return rc;
	if(enqueue_buffers(v, dir))
	{
		free_buffers(v);
		return VIDEOCODEC_ERR_ENQUEUE_BUFFERS;
	}
	return 0;
}

static int init(VIDEOCODEC *v)
{
	int rc;
	
	v->init = 1;
	if(v->decoder)
	{
		rc = create_mmap_buffers(v, 0);
		if(rc)
			return rc;
		return 0;
	}
	rc = prepare_buffers(v, 1);
	if(rc)
		return rc;
	if(start_streaming(v, 1))
		return VIDEOCODEC_ERR_START_STREAMING;
	rc = create_mmap_buffers(v, 0);
	if(rc)
	{
		free_buffers(v);
		return rc;
	}
	return 0;
}

int videocodec_setcodec(void *v, int codec)
{
	if(set_codec((VIDEOCODEC *)v, codec))
		return VIDEOCODEC_ERR_SET_CODEC;
	return 0;
}

static int set_codecparam(VIDEOCODEC *v, int id, int value)
{
	struct v4l2_ext_control ctrl;
	struct v4l2_ext_controls ctrls;

	ctrl.id = id;
	ctrl.value = value;

	ctrls.ctrl_class = V4L2_CTRL_CLASS_MPEG;
	ctrls.count = 1;
	ctrls.controls = &ctrl;

	if(ioctl(v->fd, VIDIOC_S_EXT_CTRLS, &ctrls) == -1)
		return -1;
	return 0;
}

int videocodec_setcodecparam(void *v, int id, int value)
{
	if(set_codecparam((VIDEOCODEC *)v, id, value))
		return VIDEOCODEC_ERR_SET_CODEC;
	return 0;
}

static int enqueue_buffer(VIDEOCODEC *v, int dir, int idx)
{
	if(ioctl(v->fd, VIDIOC_QBUF, &v->buffers[dir][idx]) == -1)
		return -1;
	return 0;
}

static int dequeue_buffer(VIDEOCODEC *v, int dir, int idx)
{
	if(ioctl(v->fd, VIDIOC_DQBUF, &v->buffers[dir][idx]) == -1)
		return -1;
	return 0;
}

static int yuyv2nv12m(const char *src, int w, int h, char *dy, char *duv, unsigned bytesperline[2])
{
	int x, y;

	for(y = 0; y < h; y++)
		for(x = 0; x < w; x++)
			dy[bytesperline[0] * y + x] = src[(w * y + x) * 2];	
	for(y = 0; y < h/2; y++)
		for(x = 0; x < w/2; x++)
		{
			duv[bytesperline[1] * y + 2*x] =
				(src[(w * 2*y + 2*x) * 2 + 1] + src[(w * 2*y + w + 2*x) * 2 + 1]) / 2;
			duv[bytesperline[1] * y + 2*x+1] =
				(src[(w * 2*y + 2*x) * 2 + 3] + src[(w * 2*y + w + 2*x) * 2 + 3]) / 2;
		}
	return 0;
}

static int nv12mt2yuyv(char *dst, int w, int h, const char *dy, const char *duv, unsigned bytesperline[2])
{
	int x, y, x1, y1, mbi, ny, pos;
	const int zord[2][4] = {{0, 1, 6, 7}, {2, 3, 4, 5}};

	// Luma
	ny = w / 64;
	for(y = 0; y < h; y++)
		for(x = 0; x < w; x++)
		{
			mbi = (x & 63) + (y & 31) * 64;
			x1 = x>>6;
			y1 = y>>5;
			if(y + 32 < h)
				pos = zord[y1 & 1][x1 & 3] + (x1 / 4) * 8 + (y1 / 2) * (ny*2);
			else pos = (y1 * ny + x1);
			dst[(w * y + x) * 2] = dy[pos * 2048 + mbi];
		}
	// Chroma
	for(y = 0; y < h/2; y++)
		for(x = 0; x < w; x++)
		{
			mbi = (x & 63) + (y & 31) * 64;
			x1 = x>>6;
			y1 = y>>5;
			if(y + 32 < h)
				pos = zord[y1 & 1][x1 & 3] + (x1 / 4) * 8 + (y1 / 2) * (ny*2);
			else pos = (y1 * ny + x1);
			dst[(w * 2*y + x) * 2 + 1] = dst[(w * (2*y+1) + x) * 2 + 1] = duv[pos * 2048 + mbi];
		}
	return 0;
}

int videocodec_process(void *v, const char *inframe, unsigned inframelen, char **outframe, unsigned *outframelen, int *keyframe)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	struct timeval tv;
	fd_set fds;
	int rc;
	static int n;
	
	if(!v1->init)
	{
		rc = init(v1);
		if(rc)
			return rc;
	}
	if(!inframe)
	{
		if(!v1->ended)
		{
			struct v4l2_encoder_cmd cmd;
			memset(&cmd, 0, sizeof cmd);
			if(v1->decoder)
			{
				cmd.cmd = V4L2_DEC_CMD_STOP;
				ioctl(v1->fd, VIDIOC_DECODER_CMD, &cmd);
			} else {
				cmd.cmd = V4L2_ENC_CMD_STOP;
				ioctl(v1->fd, VIDIOC_ENCODER_CMD, &cmd);
			}
			v1->ended = 1;
		}
	} else if(inframe != (const char *)-1)
	{
		if(v1->ninframes >= v1->nbuffers[0])
		{
			if(dequeue_buffer(v1, 0, v1->curbufidx[0]))
				return VIDEOCODEC_ERR_DEQUEUE_BUFFERS;
		}
		if(v1->decoder)
		{
			memcpy(v1->pointers[0][v1->curbufidx[0]][0], inframe, inframelen);
			v1->buffers[0][v1->curbufidx[0]].m.planes[0].bytesused = inframelen;
		} else yuyv2nv12m(inframe, v1->w, v1->h, v1->pointers[0][v1->curbufidx[0]][0],
				v1->pointers[0][v1->curbufidx[0]][1], v1->bytesperline);
		v1->buffers[0][v1->curbufidx[0]].timestamp.tv_sec = n / 1000000;
		v1->buffers[0][v1->curbufidx[0]].timestamp.tv_usec = n % 1000000;
		n += 40000;
		v1->buffers[0][v1->curbufidx[0]].index = v1->curbufidx[0];
		if(enqueue_buffer(v1, 0, v1->curbufidx[0]))
			return VIDEOCODEC_ERR_ENQUEUE_BUFFERS;
		v1->curbufidx[0] = (v1->curbufidx[0] + 1) % v1->nbuffers[0];
		v1->ninframes++;
		if(!v1->started)
		{
			if(start_streaming(v1, 0))
				return VIDEOCODEC_ERR_START_STREAMING;
			v1->started = 1;
			if(v1->decoder)
			{
				if(get_format(v))
					return VIDEOCODEC_ERR_SET_FORMAT;
				rc = prepare_buffers(v, 1);
				if(rc)
					return rc;
				if(dequeue_buffer(v1, 0, v1->curbufidx[0]))
					return VIDEOCODEC_ERR_DEQUEUE_BUFFERS;
				v1->curbufidx[0] = 0;
				v1->ninframes = 0;
				n -= 40000;
				if(start_streaming(v1, 1))
					return VIDEOCODEC_ERR_START_STREAMING;
			}
		}
	}
	
	tv.tv_sec = 0;
	tv.tv_usec = 0;
	FD_ZERO(&fds);
	FD_SET(v1->fd, &fds);
	select(v1->fd + 1, &fds, 0, 0, &tv);
	if(FD_ISSET(v1->fd, &fds))
	{
		if(dequeue_buffer(v1, 1, v1->curbufidx[1]))
			return VIDEOCODEC_ERR_DEQUEUE_BUFFERS;
		if(enqueue_buffer(v1, 1, (v1->curbufidx[1] + v1->nbuffers[1] - 1) % v1->nbuffers[1]))
			return VIDEOCODEC_ERR_ENQUEUE_BUFFERS;
		if(v1->decoder)
		{
			nv12mt2yuyv((char *)outframe, v1->w, v1->h, v1->pointers[1][v1->curbufidx[1]][0],
				v1->pointers[1][v1->curbufidx[1]][1], v1->bytesperline);
			*outframelen = v1->w * v1->h * 2;
		} else {
			*outframe = (char *)v1->pointers[1][v1->curbufidx[1]][0];
			*outframelen = v1->buffers[1][v1->curbufidx[1]].m.planes[0].bytesused;
		}
		if(keyframe)
			*keyframe = v1->buffers[1][v1->curbufidx[1]].flags & V4L2_BUF_FLAG_KEYFRAME;
		v1->curbufidx[1] = (v1->curbufidx[1] + 1) % v1->nbuffers[1];
	} else {
		*outframe = 0;
		*outframelen = 0;
		if(keyframe)
			*keyframe = 0;
	}
	return 0;
}

int videocodec_close(void *v)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	if(v1->fd)
	{
		stop_streaming(v1);
		free_buffers(v1);
		close(v1->fd);
	}
	if(v1->fmt_ctx)
	{
		if(v1->avformat_header_written)
			av_write_trailer(v1->fmt_ctx);
		avio_close(v1->fmt_ctx->pb);
		avformat_free_context(v1->fmt_ctx);
	}
	free(v1);
	return 0;
}

void dump_avcodec_context(AVCodecContext *c)
{
	int i;
	
	printf("codec_type = %d\n", c->codec_type);
	printf("codec_id = %d\n", c->codec_id);
	printf("codec_tag = %u\n", c->codec_tag);
	printf("stream_codec_tag = %u\n", c->stream_codec_tag);
	printf("bitrate = %d\n", c->bit_rate);
	printf("bit_rate_tolerance = %d\n", c->bit_rate_tolerance);
	printf("global_quality = %d\n", c->global_quality);
	printf("compression_level = %d\n", c->compression_level);
	printf("flags = %d\n", c->flags);
	printf("flags2 = %d\n", c->flags2);
	printf("extradata = ");
	for(i = 0; i < c->extradata_size; i++)
		printf("0x%02X, ", c->extradata[i]);
	printf("\nextradata_size = %d\n", c->extradata_size);
	printf("time_base.num = %d\n", c->time_base.num);
	printf("time_base.den = %d\n", c->time_base.den);
	printf("ticks_per_frame = %d\n", c->ticks_per_frame);
	printf("delay = %d\n", c->delay);
	printf("width = %d\n", c->width);
	printf("height = %d\n", c->height);
	printf("coded_width = %d\n", c->coded_width);
	printf("coded_height = %d\n", c->coded_height);
	printf("gop_size = %d\n", c->gop_size);
	printf("pix_fmt = %d\n", c->pix_fmt);
	printf("me_method = %d\n", c->me_method);
	printf("max_b_frames = %d\n", c->max_b_frames);
	printf("b_quant_factor = %f\n", c->b_quant_factor);
	printf("rc_strategy = %d\n", c->rc_strategy);
	printf("b_frame_strategy = %d\n", c->b_frame_strategy);
	printf("b_quant_offset = %f\n", c->b_quant_offset);
	printf("has_b_frames = %d\n", c->has_b_frames);
	printf("mpeg_quant = %d\n", c->mpeg_quant);
	printf("i_quant_factor = %f\n", c->i_quant_factor);
	printf("i_quant_offset = %f\n", c->i_quant_offset);
	printf("slice_count = %d\n", c->slice_count);
	printf("sample_aspect_ratio.num = %d\n", c->sample_aspect_ratio.num);
	printf("sample_aspect_ratio.den = %d\n", c->sample_aspect_ratio.den);
}

int videocodec_createfile(void *v, const char *url, const char *format)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	AVStream *stream;
	AVCodec *codec;
	
	if(!avformat_init)
	{
		av_register_all();		
		avformat_network_init();
		avformat_init = 1;
	}
	v1->fmt_ctx = avformat_alloc_context();
	if(!v1->fmt_ctx)
		return VIDEOCODEC_ERR_AVFORMAT;
	v1->fmt_ctx->oformat = av_guess_format(format, format ? 0 : url, 0);
	if(!v1->fmt_ctx->oformat)
	{
		avformat_free_context(v1->fmt_ctx);
		v1->fmt_ctx = 0;
		return VIDEOCODEC_ERR_AVFORMAT;
	}
	v1->writeextradata = 1;
	if(format)
	{
		if(!strcmp(format, "mp4"))
			v1->writeextradata = 0;
	} else if(url && !strcmp(url + strlen(url) - 4, ".mp4"))
		v1->writeextradata = 0;
	v1->fmt_ctx->priv_data = NULL;
	strncpy(v1->fmt_ctx->filename, url, sizeof(v1->fmt_ctx->filename));
	if(avio_open(&v1->fmt_ctx->pb, url, AVIO_FLAG_WRITE) < 0)
	{
		avformat_free_context(v1->fmt_ctx);
		v1->fmt_ctx = 0;
		return VIDEOCODEC_ERR_BADURL;
	}
	codec = avcodec_find_decoder(AV_CODEC_ID_H264);
	stream = avformat_new_stream(v1->fmt_ctx, codec);
	if (v1->fmt_ctx->oformat->flags & AVFMT_GLOBALHEADER)
		stream->codec->flags |= CODEC_FLAG_GLOBAL_HEADER;
	if(v1->framerate)
		v1->duration = 90000 / v1->framerate;
	else v1->duration = 3750;
	stream->codec->width = v1->w;
	stream->codec->height = v1->h;
	stream->codec->coded_width = v1->w;
	stream->codec->coded_height = v1->h;
	stream->codec->time_base.num = 1;
	stream->codec->time_base.den = 90000;
	stream->codec->ticks_per_frame = 2;
	stream->codec->pix_fmt = 0;
	stream->time_base.num = 1;
	stream->time_base.den = 90000;
	stream->avg_frame_rate.num = v1->framerate;
	stream->avg_frame_rate.den = 1;
	stream->codec->codec_id = AV_CODEC_ID_H264;
	stream->codec->sample_aspect_ratio.num = 1;
	stream->codec->sample_aspect_ratio.den = 1;
	stream->sample_aspect_ratio = stream->codec->sample_aspect_ratio;
	return 0;
}

int videocodec_writefile(void *v, const char *inframe, unsigned inframelen)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	AVStream *stream;
	AVPacket pkt;
	int rc, keyframe;
	char *outframe;
	unsigned outframelen;
	
	if(!v1->fmt_ctx)
		return VIDEOCODEC_ERR_NOTINIT;
	stream = v1->fmt_ctx->streams[0];
	if(!stream)
		return VIDEOCODEC_ERR_NOTINIT;
	rc = videocodec_process(v, inframe, inframelen, &outframe, &outframelen, &keyframe);
	if(rc < 0)
		return rc;
	if(!inframe && !outframe)
		return VIDEOCODEC_ERR_FINISHED;
	if(outframelen)
	{
		if(!v1->avformat_header_written)
		{
			stream->codec->extradata = av_malloc(outframelen);
			stream->codec->extradata_size = outframelen;
			memcpy(stream->codec->extradata, outframe, outframelen);
			//dump_avcodec_context(stream->codec);
			if(avformat_write_header(v1->fmt_ctx, NULL) < 0)
			{
				avio_close(v1->fmt_ctx->pb);
				avformat_free_context(v1->fmt_ctx);
				v1->fmt_ctx = 0;
				return VIDEOCODEC_ERR_WRITE;
			}
			v1->avformat_header_written = 1;
			return 0;
		}
		if(v1->writeextradata && keyframe)
		{
			memset(&pkt, 0, sizeof(pkt));
			pkt.pts = v1->noutframes * v1->duration;
			pkt.dts = v1->noutframes * v1->duration;
			pkt.flags = keyframe ? AV_PKT_FLAG_KEY : 0;
			v1->noutframes++;
			pkt.duration = v1->duration;
			pkt.pos = -1;
			pkt.data = (uint8_t *)stream->codec->extradata;
			pkt.size = stream->codec->extradata_size;
			pkt.stream_index = 0;
			if(av_write_frame(v1->fmt_ctx, &pkt) < 0)
				return VIDEOCODEC_ERR_WRITE;
		}
		memset(&pkt, 0, sizeof(pkt));
		pkt.pts = v1->noutframes * v1->duration;
		pkt.dts = v1->noutframes * v1->duration;
		v1->noutframes++;
		pkt.duration = v1->duration;
		pkt.pos = -1;
		pkt.data = (uint8_t *)outframe;
		pkt.size = outframelen;
		pkt.stream_index = 0;
		if(av_write_frame(v1->fmt_ctx, &pkt) < 0)
			return VIDEOCODEC_ERR_WRITE;
	}
	return 0;
}

int videocodec_writeencfile(void *v, const char *inframe, unsigned inframelen)
{
	VIDEOCODEC *v1 = (VIDEOCODEC *)v;
	AVStream *stream;
	AVPacket pkt;
	
	if(!v1->fmt_ctx)
		return VIDEOCODEC_ERR_NOTINIT;
	stream = v1->fmt_ctx->streams[0];
	if(!stream)
		return VIDEOCODEC_ERR_NOTINIT;
	if(!v1->avformat_header_written)
	{
		stream->codec->extradata = av_malloc(inframelen);
		stream->codec->extradata_size = inframelen;
		memcpy(stream->codec->extradata, inframe, inframelen);			
		if(avformat_write_header(v1->fmt_ctx, NULL) < 0)
		{
			avio_close(v1->fmt_ctx->pb);
			avformat_free_context(v1->fmt_ctx);
			v1->fmt_ctx = 0;
			return VIDEOCODEC_ERR_WRITE;
		}
		v1->avformat_header_written = 1;
		return 0;
	}
	if(v1->writeextradata && (v1->noutframes % 25) == 0)
	{
		memset(&pkt, 0, sizeof(pkt));
		pkt.pts = v1->noutframes * v1->duration;
		pkt.dts = v1->noutframes * v1->duration;
		v1->noutframes++;
		pkt.duration = v1->duration;
		pkt.pos = -1;
		pkt.data = (uint8_t *)stream->codec->extradata;
		pkt.size = stream->codec->extradata_size;
		pkt.stream_index = 0;
		if(av_write_frame(v1->fmt_ctx, &pkt) < 0)
			return VIDEOCODEC_ERR_WRITE;
	}
	memset(&pkt, 0, sizeof(pkt));
	pkt.pts = v1->noutframes * v1->duration;
	pkt.dts = v1->noutframes * v1->duration;
	v1->noutframes++;
	pkt.duration = v1->duration;
	pkt.pos = -1;
	pkt.data = (uint8_t *)inframe;
	pkt.size = inframelen;
	pkt.stream_index = 0;
	
	if(av_write_frame(v1->fmt_ctx, &pkt) < 0)
		return VIDEOCODEC_ERR_WRITE;
	return 0;
}

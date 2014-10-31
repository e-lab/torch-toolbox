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
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>

/* video decoder on DMA memory */
static int stream_idx;
static AVFormatContext *pFormatCtx;
static AVCodecContext *pCodecCtx;
static AVFrame *pFrame_yuv;
static AVFrame *pFrame_intm;

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

/*
 * Free and close video decoder
 */
int video_decoder_exit(lua_State * L)
{
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
		avformat_close_input(&pFormatCtx);

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

	/* pass input arguments */
	const char *fpath = lua_tostring(L, 1);
	const char *src_type = lua_tostring(L, 2);

	/* register all formats and codecs */
	av_register_all();
	avformat_network_init();

	/* use the input format if provided, otherwise guess */
	AVInputFormat *iformat = av_find_input_format(src_type);

	/* open video file */
	if (avformat_open_input(&pFormatCtx, fpath, iformat, NULL) != 0) {
		fprintf(stderr, "<video_decoder> no video was provided.\n");
		video_decoder_exit(NULL);
		lua_pushboolean(L, 0);
		return 1;
	}

	/* retrieve stream information */
	if (avformat_find_stream_info(pFormatCtx, NULL) < 0) {
		fprintf(stderr,
			"<video_decoder> no stream information was founded.\n");
		video_decoder_exit(NULL);
		lua_pushboolean(L, 0);
		return 1;
	}

	/* dump information about file onto standard error */
	av_dump_format(pFormatCtx, 0, fpath, 0);

	/* find the first video stream */
	stream_idx = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++) {
		if (pFormatCtx->streams[i]->codec->codec_type == AVMEDIA_TYPE_VIDEO) {
			stream_idx = i;
			break;
		}
	}
	if (stream_idx == -1) {
		fprintf(stderr,
			"<video_decoder> could not find a video stream.\n");
		video_decoder_exit(NULL);
		lua_pushboolean(L, 0);
		return 1;
	}

	/* get a pointer to the codec context for the video stream */
	pCodecCtx = pFormatCtx->streams[stream_idx]->codec;

	/* pre-calculate lookup table */
	video_decoder_yuv420p_rgbp_LUT();

	/* find the decoder for the video stream */
	pCodec = avcodec_find_decoder(pCodecCtx->codec_id);
	if (pCodec == NULL) {
		fprintf(stderr,
			"<video_decoder> the codec is not supported.\n");
		video_decoder_exit(NULL);
		lua_pushboolean(L, 0);
		return 1;
	}

	/* open codec */
	if (avcodec_open2(pCodecCtx, pCodec, NULL) < 0) {
		fprintf(stderr, "<video_decoder> could not open the codec.\n");
		video_decoder_exit(NULL);
		lua_pushboolean(L, 0);
		return 1;
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
	double frame_rate = pFormatCtx->streams[i]->r_frame_rate.num /
		(double) pFormatCtx->streams[i]->r_frame_rate.den;

	/* return frame dimensions */
	lua_pushboolean(L, 1);
	lua_pushnumber(L, pCodecCtx->height);
	lua_pushnumber(L, pCodecCtx->width);
	if (pFormatCtx->streams[i]->nb_frames > 0) {
		lua_pushnumber(L, pFormatCtx->streams[i]->nb_frames);
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

/* This function decodes each frame on the fly. Frame is decoded and saved
 * into "pFrame_yuv" as yuv420p, then converted to planar RGB in
 * "pFrame_intm". Finally, memcpy copies all from "pFrame_intm" to
 * dst tensor, which is measured faster than direct writing of planar RGB
 * to dst tensor.
 */
static int video_decoder_rgb(lua_State * L)
{
	AVPacket packet;
	int c, frame_decoded;
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
		fprintf(stderr,
			"<video_decoder>: cannot process tensor type %s\n", tname);
		lua_pushboolean(L, 0);
		return 1;
	}

	if ((3 != dim) || (3 != size[0])) {
		fprintf(stderr,
			"<video_decoder>: cannot process tensor of this dimension and size\n");
		lua_pushboolean(L, 0);
		return 1;
	}

	/* read frames and save first five frames to disk */
	while (av_read_frame(pFormatCtx, &packet) >= 0) {

		/* is this a packet from the video stream? */
		if (packet.stream_index == stream_idx) {

			/* decode video frame */
			avcodec_decode_video2(pCodecCtx, pFrame_yuv, &frame_decoded, &packet);

			/* check if frame is decoded */
			if (frame_decoded) {

				/* convert YUV420p to planar RGB */
				video_decoder_yuv420p_rgbp(pFrame_yuv, pFrame_intm);

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

static int video_decoder_yuv(lua_State * L)
{
	AVPacket packet;
	int c, frame_decoded;
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
		fprintf(stderr,
			"<video_decoder>: cannot process tensor type %s\n", tname);
		lua_pushboolean(L, 0);
		return 1;
	}

	if ((3 != dim) || (3 != size[0])) {
		fprintf(stderr,
			"<video_decoder>: cannot process tensor of this dimension and size\n");
		lua_pushboolean(L, 0);
		return 1;
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

static const struct luaL_reg video_decoder[] = {
	{"init", video_decoder_init},
	{"frame_rgb", video_decoder_rgb},
	{"frame_yuv", video_decoder_yuv},
	{"exit", video_decoder_exit},
	{NULL, NULL}
};

int luaopen_libvideo_decoder(lua_State * L)
{
	luaL_register(L, "libvideo_decoder", video_decoder);
	return 1;
}

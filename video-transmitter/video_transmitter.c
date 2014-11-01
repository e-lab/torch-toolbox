#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/mathematics.h>
#include <sys/times.h>
#include <stdio.h>
#include <luaT.h>
#include <TH/TH.h>
#include <unistd.h>

/* video transmitter via UDP */
static AVFormatContext   *pFormatCtx;
static AVCodecContext    *pCodecCtx;
static AVFrame           *pFrame_yuv;
static AVFrame           *pFrame_rgb;
static AVStream          *pVideoStream;
static AVPacket          packet_tx;
static uint8_t           *video_outbuf;
static int               frame_count, writestarted;
static const int         video_outbuf_size = 200000;

/* rgbp-to-yuv420p lookup table */
static uint8_t SATY[768];
static uint8_t SATUV[768];


/* This function calculates a lookup table for rgbp-to-yuv420p conversion
 * Written by Marko Vitez.
 */
static void video_transmitter_rgbp_yuv420p_LUT()
{
	int i;

	/* Y goes from 16 to 235 */
	memset(SATY, 16, 256+16);
	for(i = 16; i < 235; i++)
		SATY[256+i] = i;
	memset(SATY+256+235, 235, 768-(256+235));

	/* U and V go from 16 to 240 */
	memset(SATUV, 16, 256+16);
	for(i = 16; i < 240; i++)
		SATUV[256+i] = i;
	memset(SATUV+256+240, 240, 768-(256+240));
}

/* This function is a main function for converting color space from planar RGB to yuv420p.
 * It utilizes a lookup table method for fast conversion. Written by Marko Vitez.
 */
static void video_transmitter_rgbp_yuv420p(AVFrame *rgb, AVFrame *yuv)
{
	int i, j, rM, gM, bM;
	int h  = rgb->height;
	int w  = rgb->width;
	int wy = yuv->linesize[0];
	int wu = yuv->linesize[1];
	int wv = yuv->linesize[2];
	int w2 = w;
	uint8_t *y, *u, *v;
	uint8_t *r1 = rgb->data[0];
	uint8_t *g1 = rgb->data[1];
	uint8_t *b1 = rgb->data[2];
	w /= 2;
	h /= 2;

	for (i = 0; i < h; i++) {
		y = yuv->data[0] + wy*i*2;
		u = yuv->data[1] + wu*i;
		v = yuv->data[2] + wv*i;

		for (j = 0; j < w; j++) {
			y[0]    = SATY[(19*r1[0]    + 160*g1[0]    + 41*b1[0]) / 256 + 16 + 256];
			y[1]    = SATY[(19*r1[1]    + 160*g1[1]    + 41*b1[1]) / 256 + 16 + 256];
			y[wy]   = SATY[(19*r1[w2]   + 160*g1[w2]   + 41*b1[w2]) / 256 + 16 + 256];
			y[wy+1] = SATY[(19*r1[w2+1] + 160*g1[w2+1] + 41*b1[w2+1]) / 256 + 16 + 256];

			rM = r1[0] + r1[1] + r1[w2] + r1[w2+1];
			gM = g1[0] + g1[1] + g1[w2] + g1[w2+1];
			bM = b1[0] + b1[1] + b1[w2] + b1[w2+1];

			u[j] = SATUV[(-10*rM -88*gM + 99*bM) / 1024 + 128 + 256];
			v[j] = SATUV[(130*rM -104*gM  -26*bM) / 1024 + 128 + 256];

			y  += 2;
			r1 += 2;
			g1 += 2;
			b1 += 2;
		}
		r1 += w2;
		g1 += w2;
		b1 += w2;
	}
}

#ifndef NEWAPI
/*
 * Encode video frame for transmission. Written by Marko Vitez
 */
static int avcodec_transmitter_encode_video2(AVCodecContext *avctx, AVPacket *avpkt, const AVFrame *frame, int *got_packet_ptr)
{
	int out_size = avcodec_encode_video(avctx, video_outbuf, video_outbuf_size, frame);

	if (out_size > 0) {
		av_init_packet(avpkt);
		avpkt->pts = av_rescale_q(avctx->coded_frame->pts, avctx->time_base, pVideoStream->time_base);

		if (avctx->coded_frame->key_frame)
			avpkt->flags |= AV_PKT_FLAG_KEY;
		avpkt->stream_index = pVideoStream->index;
		avpkt->data = video_outbuf;
		avpkt->size = out_size;
		*got_packet_ptr = 1;
	} else {
		*got_packet_ptr = 0;
	}
	return 0;
}
#endif

/*
 * Free and close video transmitter. Written by Marko Vitez
 */
int video_transmitter_close(lua_State * L)
{
	/* free the AVFrame structures */
	if (pFrame_yuv) {
		av_free(pFrame_yuv);
		pFrame_yuv = 0;
	}
	if (pFrame_rgb) {
		av_free(pFrame_rgb);
		pFrame_rgb = 0;
	}
	if (video_outbuf) {
		av_free(video_outbuf);
		video_outbuf = 0;
	}

	/* close the codec and video stream */
	if (pCodecCtx) {
		avcodec_close(pCodecCtx);
		pCodecCtx = 0;
	}
	if (pVideoStream) {
		if (writestarted)
			av_write_trailer(pFormatCtx);
		av_free(pVideoStream);
		pVideoStream = 0;
	}
	if (pFormatCtx->pb)
		avio_close(pFormatCtx->pb);
	if (pFormatCtx)
		av_free(pFormatCtx);
	av_free_packet(&packet_tx);

	return 0;
}

/*
 * Init video frame transmitter via UDP. Written by Marko Vitez
 */
static int video_transmitter_init(lua_State * L)
{
	uint8_t *buffer_in, *buffer_out;
	AVOutputFormat *fmt;
	AVCodec *codec = NULL;

	/* pass input arguments */
	THByteTensor *tensor = luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));
	if (tensor == NULL) {
		fprintf(stderr, "<video_transmitter> no tensor was passsed in.\n");
		lua_pushnil(L);
		return 1;
	}
	const char *url = lua_tostring(L, 2);
	int fps = luaL_checkinteger(L, 3);
	int quantizer = luaL_checkinteger(L, 4);
	int height = tensor->size[1];
	int width  = tensor->size[2];

	/* reset status counters */
	frame_count = 0;
	writestarted = 0;

	/* pre-calculate a lookup table */
	video_transmitter_rgbp_yuv420p_LUT();

	/* register all formats and codecs */
	av_register_all();

	/* init global network components */
	avformat_network_init();

	/* setup output format and codec contexts */
	fmt = av_guess_format("mpegts", 0, 0);
	fmt->video_codec = CODEC_ID_MPEG2VIDEO;
	pFormatCtx = avformat_alloc_context();
	pFormatCtx->oformat = fmt;
	strcpy(pFormatCtx->filename, url);
	pVideoStream = avformat_new_stream(pFormatCtx, 0);
	pCodecCtx = pVideoStream->codec;
	avcodec_get_context_defaults3(pCodecCtx, codec);
	pCodecCtx->codec_id       = fmt->video_codec;
	pCodecCtx->codec_type     = AVMEDIA_TYPE_VIDEO;
	pVideoStream->sample_aspect_ratio = pCodecCtx->sample_aspect_ratio;
	pCodecCtx->width          = width;
	pCodecCtx->height         = height;
	pCodecCtx->time_base.den  = fps;
	pCodecCtx->time_base.num  = 1;
	pCodecCtx->pix_fmt        = PIX_FMT_YUV420P;
	pCodecCtx->global_quality = FF_QP2LAMBDA * quantizer;
	pCodecCtx->gop_size       = 12;
	pCodecCtx->max_b_frames   = 2;
	pCodecCtx->flags         |= CODEC_FLAG_QSCALE;
	av_dump_format(pFormatCtx, 0, url, 1);

	/* find the codec */
	codec = avcodec_find_encoder(pCodecCtx->codec_id);
	if (avcodec_open2(pCodecCtx, codec, 0)) {
		fprintf(stderr, "<video_transmitter> could not open the codec.\n");
		video_transmitter_close(NULL);
		lua_pushnil(L);
		return 1;
	}

	/* open a channel to the specified URL */
	if (avio_open(&pFormatCtx->pb, url, AVIO_FLAG_WRITE) < 0) {
		fprintf(stderr, "<video_transmitter> could not write a channel to the URL.\n");
		video_transmitter_close(NULL);
		lua_pushnil(L);
		return 1;
	}

	/* write the stream header to an output media */
	if (avformat_write_header(pFormatCtx, 0)) {
		fprintf(stderr, "<video_transmitter> could not write a AVFormat header.\n");
		video_transmitter_close(NULL);
		lua_pushnil(L);
		return 1;
	}

	/* clear transmission packaet */
	memset(&packet_tx, 0, sizeof(packet_tx));

	/* allocate video buffer for transmission */
	video_outbuf = (uint8_t *) av_malloc(video_outbuf_size);

	/* allocate AVFrame for the incoming tensor from Torch */
	buffer_in  = (uint8_t *) THByteTensor_data(tensor);
	pFrame_rgb = avcodec_alloc_frame();
	pFrame_rgb->height  = height;
	pFrame_rgb->width   = width;
	pFrame_rgb->data[0] = buffer_in;
	pFrame_rgb->data[1] = buffer_in + height*width;
	pFrame_rgb->data[2] = buffer_in + height*width*2;
	pFrame_rgb->quality = (float)(FF_QP2LAMBDA * quantizer);

	/* allocate AVFrame for encoding */
	buffer_out = (uint8_t *) malloc(width*height*3/2);
	pFrame_yuv = avcodec_alloc_frame();
	pFrame_yuv->height  = height;
	pFrame_yuv->width   = width;
	pFrame_yuv->data[0] = buffer_out;
	pFrame_yuv->data[1] = buffer_out + width*height;
	pFrame_yuv->data[2] = buffer_out + width*height*5/4;
	pFrame_yuv->linesize[0] = width;
	pFrame_yuv->linesize[1] = width/2;
	pFrame_yuv->linesize[2] = width/2;
	pFrame_yuv->quality = (float)(FF_QP2LAMBDA * quantizer);

	return 0;
}

/*
 * Transmit each frame. Written by Marko Vitez
 */
static int video_transmitter_forward(lua_State * L)
{
	int got_packet, rc;

	/* convert Planar RGB to YUV420p */
	video_transmitter_rgbp_yuv420p(pFrame_rgb, pFrame_yuv);

	/* check if codec context is available*/
	if(!pCodecCtx) {
		fprintf(stderr, "<video_transmitter> the codec context is not available.\n");
		video_transmitter_close(NULL);
		lua_pushnil(L);
		return 1;
	}

	/* track the nb of frames sent */
	pFrame_yuv->pts = frame_count;

	/* decode and sent each frame */
	rc = avcodec_transmitter_encode_video2(pCodecCtx, &packet_tx, pFrame_yuv, &got_packet);
	if(!rc && got_packet) {
		writestarted = 1;
		av_write_frame(pFormatCtx, &packet_tx);
	}
	frame_count++;

	return 0;
}

static const struct luaL_reg video_transmitter[] = {
	{"forward", video_transmitter_forward},
	{"close", video_transmitter_close},
	{"init", video_transmitter_init},
	{NULL, NULL}
};

int luaopen_libvideo_transmitter(lua_State * L)
{
	luaL_register(L, "libvideo_transmitter", video_transmitter);
	return 1;
}

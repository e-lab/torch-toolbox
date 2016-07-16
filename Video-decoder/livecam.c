#include <luaT.h>
#include <TH/TH.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <string.h>
#include <sys/ioctl.h>
#include <ft2build.h>
#include <libswscale/swscale.h>
#include FT_FREETYPE_H
#include "videocap.h"
#include "libgl.h"

typedef unsigned char uint8_t;
static int frame_width, frame_height, vcap_fps, win_x, win_y, win_width, win_height;
static char *win_name;
static void *vcap, *vcap_frame;
#define NBUFFERS 4
static char *frames[NBUFFERS];
static int curframe;
#define MAXITEMS 100
static int nitems = 0;
typedef struct {
	int x, y, w, h;
} RECT;
struct ITEM {
	int type;
	RECT rect;
	int width, color;
	char text[100];
	uint8_t *bitmap;
};
static struct ITEM items[MAXITEMS];
static FT_Library ft_lib;
static FT_Face ft_face;

static int videocap_init(lua_State *L)
{
	const char *device = lua_tostring(L, 1);
	int w = lua_tointeger(L, 2);
	int h = lua_tointeger(L, 3);
	int rc;

	if(vcap)
		videocap_close(vcap);
	vcap = videocap_open(device);
	vcap_fps = lua_tointeger(L, 4);
	if(!vcap)
	{
		luaL_error(L, "Error opening device %s", device);
	}
	rc = videocap_startcapture(vcap, w, h, V4L2_PIX_FMT_YUYV, vcap_fps, NBUFFERS);
	if(rc < 0)
	{
		videocap_close(vcap);
		vcap = 0;
		luaL_error(L, "Error %d starting capture", rc);
	}
	vcap_frame = malloc(w * h * 2);
	frame_width = w;
	frame_height = h;
	return 0;
}

#define BYTE2FLOAT 0.003921568f // 1/255
static short TB_YUR[256], TB_YUB[256], TB_YUGU[256], TB_YUGV[256], TB_Y[256];
static unsigned char TB_SAT[1024 + 1024 + 256];

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

void yuyv2torchRGB(const unsigned char *frame, unsigned char *dst_byte, int imgstride, int rowstride, int w, int h)
{
	int i, j, w2 = w / 2;
	unsigned char *dst;
	const unsigned char *src;

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

void yuyv2torchfloatRGB(const unsigned char *frame, float *dst_float, int imgstride, int rowstride, int w, int h)
{
	int i, j, w2 = w / 2;
	float *dst;
	const unsigned char *src;

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

static int frame_rgb(lua_State *L)
{
	int dim = 0;
	long *stride = NULL;
	long *size = NULL;
	float *dst_float = NULL;
	unsigned char *dst_byte = NULL;

	const char *tname = luaT_typename(L, 1);
	if (strcmp("torch.ByteTensor", tname) == 0)
	{
		THByteTensor *frame = luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));
		dst_byte = THByteTensor_data(frame);
		dim = frame->nDimension;
		stride = &frame->stride[0];
		size = &frame->size[0];
	} else if (strcmp("torch.FloatTensor", tname) == 0)
	{
		THFloatTensor *frame = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
		dst_float = THFloatTensor_data(frame);
		dim = frame->nDimension;
		stride = &frame->stride[0];
		size = &frame->size[0];
	} else luaL_error(L, "Cannot process tensor type %s", tname);

	if ((3 != dim) || (3 != size[0]))
		luaL_error(L, "Cannot process tensor of this dimension and size");

	if(!vcap)
		luaL_error(L, "Call capture first");
	while(!frames[curframe])
		usleep(100000);
	if(dst_byte)
		yuyv2torchRGB((unsigned char *)frames[curframe], dst_byte, stride[0], stride[1], frame_width, frame_height);
	else yuyv2torchfloatRGB((unsigned char *)frames[curframe], dst_float, stride[0], stride[1], frame_width, frame_height);
	return 0;
}

static int frame_y(lua_State *L)
{
	int dim = 0;
	long *dst_stride = NULL;
	long *dst_size = NULL;
	unsigned char *dst_byte = NULL;
	struct SwsContext *sws_ctx;
	const uint8_t *srcslice[3];
	uint8_t *dstslice[3];
	int srcstride[3], dststride[3];

	const char *tname = luaT_typename(L, 1);
	if (strcmp("torch.ByteTensor", tname) == 0)
	{
		THByteTensor *frame = luaT_toudata(L, 1, luaT_typenameid(L, "torch.ByteTensor"));
		dst_byte = THByteTensor_data(frame);
		dim = frame->nDimension;
		dst_stride = &frame->stride[0];
		dst_size = &frame->size[0];
	} else luaL_error(L, "Cannot process tensor type %s", tname);

	if(2 != dim)
		luaL_error(L, "Cannot process tensor of this dimension and size");

	srcslice[0] = (unsigned char *)frames[curframe];
	srcstride[0] = frame_width * 2;
	dstslice[0] = dst_byte;
	dststride[0] = dst_stride[0];

	sws_ctx = sws_getContext(frame_width, frame_height, AV_PIX_FMT_YUYV422, dst_size[1], dst_size[0], AV_PIX_FMT_GRAY8, SWS_FAST_BILINEAR, 0, 0, 0);
	sws_scale(sws_ctx, srcslice, srcstride, 0, frame_height, dstslice, dststride);
	sws_freeContext(sws_ctx);
	return 0;
}

static void PrepareText(struct ITEM *item)
{
	int i, j, k, stride, bwidth, bheight, asc, desc;
	const char *s = item->text;
	int size = item->width;
	unsigned color = item->color;
	unsigned char alpha = (color >> 24);
	unsigned char red = (color >> 16);
	unsigned char green = (color >> 8);
	unsigned char blue = color;
	FT_Set_Char_Size(ft_face, 0, item->width * 64, 0, 0 );
	int scale = ft_face->size->metrics.y_scale;
	asc = FT_MulFix(ft_face->ascender, scale) / 64;
	desc = -FT_MulFix(ft_face->descender, scale) / 64;
	bheight = asc + desc;
	bwidth = 2 * size * strlen(s);
	unsigned char *bitmap = (unsigned char *)calloc(bheight * bwidth, 4);
	stride = strlen(s) * size * 2 * 4;
	FT_Set_Char_Size(ft_face, 0, 64 * size, 0, 0);
	unsigned char *pbitmap = bitmap;
	for(i = 0; s[i]; i++)
	{
		FT_Load_Char(ft_face, s[i], FT_LOAD_RENDER);
		FT_Bitmap *bmp = &ft_face->glyph->bitmap;
		for(j = 0; j < bmp->rows; j++)
			for(k = 0; k < bmp->width; k++)
			{
				pbitmap[(j + asc - ft_face->glyph->bitmap_top) * stride + 4 * (k + ft_face->glyph->bitmap_left) + 0] = red;
				pbitmap[(j + asc - ft_face->glyph->bitmap_top) * stride + 4 * (k + ft_face->glyph->bitmap_left) + 1] = green;
				pbitmap[(j + asc - ft_face->glyph->bitmap_top) * stride + 4 * (k + ft_face->glyph->bitmap_left) + 2] = blue;
				pbitmap[(j + asc - ft_face->glyph->bitmap_top) * stride + 4 * (k + ft_face->glyph->bitmap_left) + 3] =
					bmp->buffer[j * bmp->pitch + k] * alpha / 255;
			}
		pbitmap += 4 * (ft_face->glyph->advance.x / 64);
	}
	//bwidth = (pbitmap - bitmap) / 4;
	item->rect.w = bwidth;
	item->rect.h = bheight;
	if(item->bitmap)
		free(item->bitmap);
	item->bitmap = bitmap;
	item->type = 3;
}

static void DrawItem(struct ITEM *item)
{
	unsigned color;

	switch(item->type)
	{
	case 1:
		color = (item->color & 0xff00ff00) | ((item->color & 0xff0000)>>16) | ((item->color & 0xff)<<16);
		Blt(&color, 0, 1, 1, item->rect.x, item->rect.y, item->rect.w, item->width);
		Blt(&color, 0, 1, 1, item->rect.x, item->rect.y+item->width, item->width, item->rect.h - 2 * item->width);
		Blt(&color, 0, 1, 1, item->rect.x + item->rect.w - item->width, item->rect.y + item->width, item->width, item->rect.h - 2 * item->width);
		Blt(&color, 0, 1, 1, item->rect.x, item->rect.y + item->rect.h - item->width, item->rect.w, item->width);
		break;
	case 2:
		PrepareText(item);
		if(item->type != 3)
			break;
	case 3:
		Blt(item->bitmap, 0, item->rect.w, item->rect.h, item->rect.x, item->rect.y, item->rect.w, item->rect.h);
		break;
	}
}

/*static double tm()
{
	struct timeval tv;
	double t;
	static double prevt;
	
	gettimeofday(&tv, 0);
	t = tv.tv_sec + tv.tv_usec * 1e-6 - prevt;
	prevt = tv.tv_sec + tv.tv_usec * 1e-6;
	return t;
}*/

static void *rendering_thread(void *dummy)
{
	struct timeval tv;
	int i;

	StartWindow();
	for(;;)
	{
		int rc;
		int fn = 0;

		rc = videocap_getframe(vcap, &frames[fn], &tv);
		if(rc < 0)
			break;
		curframe = fn;
		fn = (fn + 1) % NBUFFERS;
		Blt(frames[curframe], 1, frame_width, frame_height, 0, 0, win_width, win_height);
		for(i = 0; i < nitems; i++)
			DrawItem(items+i);
		Present();
	}
	fprintf(stderr, "Failed to capture from camera\n");
	return 0;
}

static int window(lua_State *L)
{
	pthread_t tid;

	win_name = strdup(lua_tostring(L, 1));
	win_x = lua_tointeger(L, 2);
	win_y = lua_tointeger(L, 3);
	win_width = lua_tointeger(L, 4);
	win_height = lua_tointeger(L, 5);

	if(CreateWindow(win_name, win_x, win_y, win_width, win_height))
		luaL_error(L, "Error creating window");
	GetWindowSize(&win_width, &win_height);
	pthread_create(&tid, 0, rendering_thread, 0);
	pthread_detach(tid);
	lua_pushinteger(L, win_width);
	lua_pushinteger(L, win_height);
	return 2;
}

static int clear(lua_State *L)
{
	nitems = 0;
	return 0;
}

static int rectangle(lua_State *L)
{
	if(nitems == MAXITEMS)
		luaL_error(L, "Maximum number of items reached");
	items[nitems].type = 1;
	items[nitems].rect.x = lua_tointeger(L, 1);
	items[nitems].rect.y = lua_tointeger(L, 2);
	items[nitems].rect.w = lua_tointeger(L, 3);
	items[nitems].rect.h = lua_tointeger(L, 4);
	items[nitems].width = lua_tointeger(L, 5);
	items[nitems].color = lua_tointeger(L, 6) | 0xff000000;
	nitems++;
	return 0;
}

static int text(lua_State *L)
{
	if(!ft_face)
		luaL_error(L, "No fonts support");
	if(nitems == MAXITEMS)
		luaL_error(L, "Maximum number of items reached");
	items[nitems].type = 2;
	items[nitems].rect.x = lua_tointeger(L, 1);
	items[nitems].rect.y = lua_tointeger(L, 2);
	strncpy(items[nitems].text, lua_tostring(L, 3), sizeof(items[nitems].text)-1);
	items[nitems].width = lua_tointeger(L, 4);
	items[nitems].color = lua_tointeger(L, 5) | 0xff000000;
	nitems++;
	return 0;
}

static void loadfont()
{
	if(FT_Init_FreeType(&ft_lib))
	{
		fprintf(stderr, "Error initializing the FreeType library\n");
		return;
	}
	if(FT_New_Face(ft_lib, "/usr/share/fonts/truetype/freefont/FreeSans.ttf", 0, &ft_face))
	{
		fprintf(stderr, "Error loading FreeSans font\n");
		return;
	}
}

static const struct luaL_reg livecam[] =
{
	{"capture", videocap_init},
	{"frame_rgb", frame_rgb},
	{"frame_y", frame_y},
	{"window", window},
	{"clear", clear},
	{"rectangle", rectangle},
	{"text", text},
	{NULL, NULL}
};

int luaopen_livecam(lua_State * L)
{
	luaL_register(L, "livecam", livecam);

	loadfont();
	video_decoder_yuv420p_rgbp_LUT();
	return 1;
}

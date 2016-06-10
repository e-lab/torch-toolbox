#include <stdio.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <libswscale/swscale.h>
#include <jpeglib.h>
#include <png.h>
#include <luaT.h>
#include <TH/TH.h>

static int logging = 0;	// Put 1 for debugging messages
static DIR *dir;
static char terminate;
typedef struct {
	char filename[255];
	uint8_t *bitmap;
	int width, height, cp;
} img_t;
static img_t *images;
static int max, greylevel;
typedef struct { int width, height; } imgsize_t;
static int nsizes;
imgsize_t *sizes;
static char initpath[256];

static void lprintf(const char *fmt, ...)
{
	if(logging)
	{
		va_list ap;
		
		va_start(ap, fmt);
		vfprintf(stdout, fmt, ap);
		va_end(ap);
	}
}

uint8_t *scale(img_t *image, int width, int height)
{
	struct SwsContext *sws_ctx;
	uint8_t *scaled_rgb;
	const uint8_t *srcslice[3];
	uint8_t *dstslice[3];
	int srcstride[3], dststride[3];
	uint8_t *buf = image->bitmap;
	int w = image->width, h = image->height, cp = image->cp;

	if(cp == 3 && w == width && h == height)
	{
		lprintf("Image %s already at desired resolution\n", image->filename);
		return buf;
	}
	scaled_rgb = (uint8_t *)malloc(3 * width * height);
	srcslice[1] = srcslice[2] = 0;
	dstslice[1] = dstslice[2] = 0;
	srcstride[1] = srcstride[2] = 0;
	dststride[1] = dststride[2] = 0;
	srcslice[0] = buf;
	srcstride[0] = cp*w;
	dststride[0] = 3*width;
	if(greylevel == -1)
	{
		dstslice[0] = scaled_rgb;
		// Crop
		if(w * height <= width * h)
		{
			// Destination image is wider of source image: cut top and bottom
			int h2 = h * height / width;
			srcslice[0] = buf + srcstride[0] * ((h - h2) / 2);
			sws_ctx = sws_getContext(w, h2, cp == 3 ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_GRAY8, width, height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
			sws_scale(sws_ctx, srcslice, srcstride, 0, h2, dstslice, dststride);
		} else {
			// Source image is wider of destination image: cut sides
			int w2 = h * width / height;
			srcslice[0] = buf + cp * ((w - w2) / 2);
			sws_ctx = sws_getContext(w2, h, cp == 3 ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_GRAY8, width, height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
			sws_scale(sws_ctx, srcslice, srcstride, 0, h, dstslice, dststride);
		}
		// Pad
	} else if(w * height <= width * h)
	{
		// Destination image is wider of source image: put grey vertical bars
		int width2 = height * w / h;
		int i;
		for(i = 0; i < height; i++)
		{
			memset(scaled_rgb + i * dststride[0], greylevel, (width - width2) / 2 * 3);
			// Don't simplify the calculations: they have to stay as they are because of truncation errors
			memset(scaled_rgb + i * dststride[0] + ((width - width2) / 2 + width2) * 3, greylevel,
				(width - ((width - width2) / 2 + width2)) * 3);
		}
		dstslice[0] = scaled_rgb + (width - width2) / 2 * 3;
		sws_ctx = sws_getContext(w, h, cp == 3 ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_GRAY8, width2, height, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
		sws_scale(sws_ctx, srcslice, srcstride, 0, h, dstslice, dststride);
	} else {
		// Source image is wider of destination image: but gray horizontal bars
		int height2 = width * h / w;
		memset(scaled_rgb, greylevel, (height - height2) / 2 * dststride[0]);
		// Don't simplify the calculations: they have to stay as they are because of truncation errors
		memset(scaled_rgb + ((height - height2) / 2 + height2) * dststride[0], greylevel,
			(height - ((height - height2) / 2 + height2)) * dststride[0]);
		dstslice[0] = scaled_rgb + (height - height2) / 2 * dststride[0];
		sws_ctx = sws_getContext(w, h, cp == 3 ? AV_PIX_FMT_RGB24 : AV_PIX_FMT_GRAY8, width, height2, AV_PIX_FMT_RGB24, SWS_FAST_BILINEAR, 0, 0, 0);
		sws_scale(sws_ctx, srcslice, srcstride, 0, h, dstslice, dststride);
	}
	sws_freeContext(sws_ctx);
	lprintf("Image %s scaled\n", image->filename);
	return scaled_rgb;
}

static int loadjpeg(const char *path, img_t *image)
{
	struct jpeg_decompress_struct cinfo;
	struct jpeg_error_mgr jerr;
	FILE *fp;
	uint8_t *buf;
	int w, h, cp;

	lprintf("loadjpeg %s\n", path);
	fp = fopen(path, "rb");
	if(!fp)
	{
		lprintf("Failed to open file %s\n", path);
		return -1;
	}
	fseek(fp, 0, SEEK_END);
	if(!ftell(fp))
	{
		fclose(fp);
		lprintf("File %s is empty\n", path);
		return -1;
	}
	fseek(fp, 0, SEEK_SET);
	cinfo.err = jpeg_std_error(&jerr);
	jpeg_create_decompress(&cinfo);
	jpeg_stdio_src(&cinfo, fp);
	jpeg_read_header(&cinfo, TRUE);
	jpeg_start_decompress(&cinfo);
	w = cinfo.output_width;
	h = cinfo.output_height;
	cp = cinfo.output_components;
	if(cp != 3 && cp != 1)
	{
		jpeg_destroy_decompress(&cinfo);
		fclose(fp);
		lprintf("Unsupported number of color planes %d\n", cinfo.output_components);
		return -1;
	}
	buf = (uint8_t *)malloc(w * h * cp);
	while(cinfo.output_scanline < h)
	{
		JSAMPROW buffer = buf + cp*w*cinfo.output_scanline;
		jpeg_read_scanlines(&cinfo, &buffer, 1);
	}
	jpeg_finish_decompress(&cinfo);
	jpeg_destroy_decompress(&cinfo);
	fclose(fp);
	lprintf("JPEG (%d,%d,%d) loaded\n", w, h, cp);
	image->bitmap = buf;
	image->width = w;
	image->height = h;
	image->cp = cp;
	return 0;
}

static int loadpng(const char *path, img_t *image)
{
	png_structp png_ptr;
	png_infop info_ptr;
	FILE *fp;
	uint8_t *buf, header[8];
	int w, h, cp, i, bit_depth;
	png_byte color_type;

	lprintf("loadpng %s\n", path);
	fp = fopen(path, "rb");
	if(!fp)
	{
		lprintf("Failed to open file %s\n", path);
		return -1;
	}
	if(fread(header, 8, 1, fp) != 1)
	{
		lprintf("Error reading header\n");
		fclose(fp);
		return -1;
	}
	if(png_sig_cmp(header, 0, 8))
	{
		lprintf("Not a png file\n");
		fclose(fp);
		return -1;
	}
	png_ptr = png_create_read_struct(PNG_LIBPNG_VER_STRING, NULL, NULL, NULL);
	info_ptr = png_create_info_struct(png_ptr);
	if(setjmp(png_jmpbuf(png_ptr)))
	{
		lprintf("Error reading png file header\n");
		fclose(fp);
		return -1;
	}
	png_init_io(png_ptr, fp);
	png_set_sig_bytes(png_ptr, 8);
	png_read_info(png_ptr, info_ptr);
	w = png_get_image_width(png_ptr, info_ptr);
	h = png_get_image_height(png_ptr, info_ptr);
	color_type = png_get_color_type(png_ptr, info_ptr);
	bit_depth  = png_get_bit_depth(png_ptr, info_ptr);
	png_read_update_info(png_ptr, info_ptr);
	switch(color_type)
	{
	case PNG_COLOR_TYPE_RGBA:
		cp = 3;
		png_set_strip_alpha(png_ptr);
		break;
	case PNG_COLOR_TYPE_RGB:
		cp = 3;
		break;
	case PNG_COLOR_TYPE_GRAY:
		if(bit_depth < 8)
			png_set_expand_gray_1_2_4_to_8(png_ptr);
		cp = 1;
		break;
	case PNG_COLOR_TYPE_GA:
		cp = 1;
		png_set_strip_alpha(png_ptr);
		break;	
	case PNG_COLOR_TYPE_PALETTE:
		cp = 3;
		png_set_expand(png_ptr);
		break;
	default:
		cp = 0;
	}
	if(cp != 3 && cp != 1)
	{
		fclose(fp);
		lprintf("Unsupported number of color planes %d\n", cp);
		return -1;
	}
	if(bit_depth == 16)
		png_set_strip_16(png_ptr);
	buf = (uint8_t *)malloc(w * h * cp);
	png_bytep *rows = (png_bytep *)malloc(sizeof(png_bytep) * h);
	if(setjmp(png_jmpbuf(png_ptr)))
	{
		lprintf("Error reading png file\n");
		fclose(fp);
		free(buf);
		free(rows);
		return -1;
	}
	for(i = 0; i < h; i++)
		rows[i] = buf + i * cp * w;
	png_read_image(png_ptr, rows);
	png_read_end(png_ptr, NULL);
	free(rows);
	fclose(fp);
	png_destroy_read_struct(&png_ptr, &info_ptr, NULL);
	lprintf("PNG (%d,%d,%d) loaded\n", w, h, cp);
	image->bitmap = buf;
	image->width = w;
	image->height = h;
	image->cp = cp;
	return 0;
}

static int loadimage(const char *path, img_t *image)
{
	lprintf("loadimage %s\n", path);
	const char *p = strrchr(path, '.');
	if(!p)
	{
		lprintf("%s has no extension\n");
		return -1;
	}
	const char *fn = strrchr(path, '/');
	if(fn)
		fn++;
	else fn = path;
	strcpy(image->filename, fn);
	if(!strcasecmp(p, ".jpeg") || !strcasecmp(p, ".jpg"))
		return loadjpeg(path, image);
	if(!strcasecmp(p, ".png"))
		return loadpng(path, image);
	lprintf("Unrecognized extension\n");
	return -1;
}

static int loadnextimage(img_t *image)
{
	struct dirent *d;

	lprintf("loadnextimage\n");
	if(terminate)
		return -1;
	if(!dir)
	{
		terminate = 1;
		return loadimage(initpath, image);
	}
	while( (d = readdir(dir)) )
		if(d->d_type == DT_REG)
		{
			char filepath[256];
			
			snprintf(filepath, sizeof(filepath), "%s/%s", initpath, d->d_name);
			if(!loadimage(filepath, image))
				return 0;
		}
	lprintf("End of directory\n");
	return -1;
}

// Convert packed RGB present in scaled_rgb to planar float
#define BYTE2FLOAT 0.003921568f // 1/255
void rgb_tofloat(float *dst_float, int imgstride, int linestride, uint8_t *src_byte, int width, int height)
{
	int c, i, j, srcstride;

	srcstride = 3 * width;
	for(c = 0; c < 3; c++)
		for(i = 0; i < height; i++)
			for(j = 0; j < width; j++)
				dst_float[j + i * linestride + c * imgstride] =
					src_byte[c + 3*j + srcstride*i] * BYTE2FLOAT;
}

// init(path, max, width, height, greylevel)
// Initializes images loading from path, which can be a directory or file name
// At most max images will be returned each time
// Images will be resized to width x height; if not given, they will be resized
// to the size of the first image found
// The images will be cropped to have the right aspect ratio, unless greylevel is
// given, in which case, they will be padded with horizontal or vertical bars
// of grey colour (0 = black, 1 = white)
//        or
// init(path, max, sizes, greylevel)
// sizes is a Nx2 tensor with the sizes to return

static int luafunc_init(lua_State *L)
{
	struct stat st;
	const char *path = lua_tostring(L, 1);
	max = lua_tointeger(L, 2);

	if(!path)
		luaL_error(L, "fastimage.init: path has to be a string");
	if(max < 1)
		luaL_error(L, "fastimage.init: max has to be a positive number");
	strcpy(initpath, path);
	const char *tname = luaT_typename(L, 3);
	if(images)
	{
		int i;

		for(i = 0; i < max; i++)
			if(images[i].bitmap)
				free(images[i].bitmap);
		free(images);
		images = 0;
	}
	if(sizes)
	{
		free(sizes);
		sizes = 0;
	}
	nsizes = 0;
	if(tname && !strcmp(tname, "torch.FloatTensor"))
	{
		THFloatTensor *t = luaT_toudata(L, 3, luaT_typenameid(L, "torch.FloatTensor"));
		if(t->nDimension == 2 && t->size[1] == 2)
		{
			int i;
			nsizes = t->size[0];
			sizes = (imgsize_t *)malloc(nsizes * sizeof(imgsize_t));
			float *data = THFloatTensor_data(t);
			for(i = 0; i < nsizes; i++)
			{
				sizes[i].width = data[i * t->stride[0]];
				sizes[i].height = data[i * t->stride[0] + 1];
			}
			if(lua_isnumber(L, 4))
				greylevel = (int)(255 * lua_tonumber(L, 4));
			else greylevel = -1;
		} else t = 0;
	} else {
		nsizes = 1;
		sizes = (imgsize_t *)malloc(sizeof(imgsize_t));
		sizes[0].width = lua_tointeger(L, 3);
		sizes[0].height = lua_tointeger(L, 4);
		if(lua_isnumber(L, 5))
			greylevel = (int)(255 * lua_tonumber(L, 5));
		else greylevel = -1;
	}
	images = (img_t *)calloc(max, sizeof(img_t));

	lprintf("fastimage.init(%s, %d, %d, %d, %d)\n", path, max, sizes[0].width, sizes[0].height, greylevel);
	terminate = 0;
	if(dir)
	{
		closedir(dir);
		dir = 0;
	}
	if(!stat(path, &st))
	{
		if(S_ISREG(st.st_mode))
			return 0;
		else if(S_ISDIR(st.st_mode))
		{
			lprintf("opendir %s\n", path);
			dir = opendir(path);
			if(!dir)
				luaL_error(L, "fastimage.init: failed to open directory %s", path);
			return 0;
		} else luaL_error(L, "fastimage.init: %s is neither a file, nor a directory", path);
	} else luaL_error(L, "fastimage.init: Cannot stat %s", path);
	return 0;
}

// load(tensor, sizeindex)
// Takes the 4D tensor and fills it with the images found in the path given by init
// If tensor is nil, a new one will be created
// Returns the given 4D tensor, reduced in size, if it's bigger of the number of found images
// or nil, if no other images are found
// sizeindex is the index into the sizes vector for the desired size
// Only if sizeindex is 0 or nil the new set of images will be loaded, otherwise the
// previous set will be returned at the desired resolution
// It also returns a table with the filenames and resolutions of the loaded images

static int luafunc_load(lua_State *L)
{
	THFloatTensor *t = 0;
	const char *tname = luaT_typename(L, 1);
	int i, index = lua_tointeger(L, 2);

	if(max == 0)
		luaL_error(L, "fastimage.init: call init first");
	if(index > nsizes)
		luaL_error(L, "Invalid size index %d", index);
	index--;
	if(index < 0)
		index = 0;
	if(tname && !strcmp(tname, "torch.FloatTensor"))
	{
		t = luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));
		if(t->nDimension == 4 && t->size[1] == 3)
		{
			if(nsizes == 1)
			{
				sizes[0].width = t->size[3];
				sizes[0].height = t->size[2];
				max = t->size[0];
			} else if(sizes[0].width != t->size[3] || sizes[0].height != t->size[2] ||
				max != t->size[0])
				t = 0;
		} else t = 0;
	}
	if(!index)
	{
		for(i = 0; i < max; i++)
			if(images[i].bitmap)
			{
				free(images[i].bitmap);
				images[i].bitmap = 0;
			}
		for(i = 0; i < max; i++)
		{
			if(loadnextimage(images + i))
				break;
		}
		if(i == 0)
		{
			lprintf("Nothing found\n");
			return 0;
		}
		if(i < max)
		{
			max = i;
			if(t)
				t = THFloatTensor_newNarrow(t, 0, 0, i);
		}	
	}
	for(i = 0; i < max; i++)
	{
		if(nsizes == 1 && (!sizes[0].width || !sizes[0].height))
		{
			lprintf("Set width = %d, height = %d\n", images[i].width, images[i].height);
			sizes[0].width = images[i].width;
			sizes[0].height = images[i].height;
		}
		if(!t)
			t = THFloatTensor_newWithSize4d(max, 3, sizes[index].height, sizes[index].width);
		uint8_t *rescaled = scale(images + i, sizes[index].width, sizes[index].height);
		rgb_tofloat(THFloatTensor_data(t) + i * t->stride[0], t->stride[1], t->stride[2], rescaled, sizes[index].width, sizes[index].height);
		if(rescaled != images[i].bitmap)
			free(rescaled);
		if(nsizes == 1 && images[i].bitmap)
		{
			// It's not necessary to keep all the images in memory, if there is only one size
			free(images[i].bitmap);
			images[i].bitmap = 0;
		}
	}
	lprintf("%d x 3 x %d x %d tensor returned\n", i, sizes[index].height, sizes[index].width);
	luaT_pushudata(L, t, "torch.FloatTensor");
	lua_createtable(L, max, 0);
	for(i = 0; i < max; i++)
	{
		lua_pushinteger(L, i+1);

		lua_createtable(L, 0, 3);
		lua_pushstring(L, "filename");
		lua_pushstring(L, images[i].filename);
		lua_settable(L, -3);
		lua_pushstring(L, "width");
		lua_pushinteger(L, images[i].width);
		lua_settable(L, -3);
		lua_pushstring(L, "height");
		lua_pushinteger(L, images[i].height);
		lua_settable(L, -3);

		lua_settable(L, -3);
	}
	return 2;
}

static const struct luaL_reg lua_functions[] = {
	{"init", luafunc_init},
	{"load", luafunc_load},
	{NULL, NULL}
};

// Initialize the library
int luaopen_fastimage(lua_State * L)
{
	luaL_register(L, "fastimage", lua_functions);
	return 1;
}

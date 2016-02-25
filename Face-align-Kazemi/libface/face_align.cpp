#include <luaT.h>
#include <TH/TH.h>
#include <exception>
#include <dlib/image_processing/frontal_face_detector.h>
#include <dlib/image_processing/render_face_detections.h>
#include <dlib/image_processing.h>
#include <dlib/image_io.h>
#include <iostream>
#include <stdio.h>
#include <math.h>

using namespace dlib;
using namespace std;

static shape_predictor sp;
static bool initialized;
static array2d<rgb_pixel> img;
static frontal_face_detector detector;
static std::vector<rectangle> dets;
static correlation_tracker tracker;

// Initialize the library and load the model file, typically
// shape_predictor_68_face_landmarks.dat
// 1st parameter: model file path
// Returns: nothing

static int lua_loadmodel(lua_State *L)
{
	const char *path = lua_tostring(L, 1);
	if(!path)
		luaL_error(L, "<loadmodel>: Missing model path");
	try {
		detector = get_frontal_face_detector();
		deserialize(path) >> sp;
	} catch (exception &e)
	{
		luaL_error(L, "<loadmodel>: %s", e.what());
	}
	initialized = true;
	return 0;
}

static void luaimagetodlib(lua_State *L, int pnum)
{
	const char *tname = luaT_typename(L, pnum);
	int w, h, stride, cstride;
	
	// Get image data
	if(!tname)
		luaL_error(L, "<facealign>: Missing image");
	if(strcmp("torch.FloatTensor", tname))
		luaL_error(L, "<facealign>: cannot process tensor type %s (must be FloatTensor)", tname);
	THFloatTensor *frame = (THFloatTensor *)luaT_toudata(L, pnum, luaT_typenameid(L, "torch.FloatTensor"));
	if(frame->nDimension != 3)
		luaL_error(L, "<facealign>: Unsupported tensor dimenstion %d (must be 3)", frame->nDimension);
	if(frame->stride[2] != 1)
		luaL_error(L, "<facealign>: Unsupported stride %d for 3rd dimension (must be 1)", frame->stride[2]);
	if(frame->size[0] != 1 && frame->size[0] != 3)
		luaL_error(L, "<facealign>: Unsupported size %d for 1st dimension (must be 1 or 3)", frame->size[0]);
	stride = frame->stride[1];
	cstride = frame->stride[0];
	h = frame->size[1];
	w = frame->size[2];
	float *data = THFloatTensor_data(frame);

	// Build dlib image
	img.set_size(h, w);
	if(frame->size[0] == 3)
	{
		for(int y = 0; y < h; y++)
			for(int x = 0; x < w; x++)
				assign_pixel(img[y][x], rgb_pixel(data[y * stride + x] * 255.0,
					data[y * stride + x + cstride] * 255.0,
					data[y * stride + x + 2*cstride] * 255.0));
	} else {
		for(int y = 0; y < h; y++)
			for(int x = 0; x < w; x++)
				assign_pixel(img[y][x], rgb_pixel(data[y * stride + x] * 255.0,
					data[y * stride + x] * 255.0,
					data[y * stride + x] * 255.0));
	}
}

// Load an image passed as a torch float tensor
// 1st parameter: tensor with the image
// Returns: number of detected faces
static int lua_facealign(lua_State *L)
{
	if(!initialized)
		luaL_error(L, "<facealign>: Call loadmodel first");
	luaimagetodlib(L, 1);
	dets = detector(img);
	lua_pushinteger(L, dets.size());
	return 1;
}

// Extract the aligned faces
// 1st parameter: index of the face starting from 0
// 2nd parameter, optional: size of the image to return (width and height will be the same), default 256
// 3rd parameter, optional: how much padding to add around the image; 0 = closely around the face,
//   0.5=double the width of the cropped area, 1=triple it, default 0.5
// Returns the colored image (first size=3) in a torch float tensor

static int lua_getimage(lua_State *L)
{
	THFloatTensor *t = (THFloatTensor*) luaT_checkudata(L, 1, "torch.FloatTensor");
	unsigned index = lua_tointeger(L, 2);
	int size = lua_tointeger(L, 3);
	double padding = lua_tonumber(L, 4);
	double shift = lua_tonumber(L, 5);

	if(dets.size() == 0)
		luaL_error(L, "<getimage>: Nothing detected");
	if(index >= dets.size())
		luaL_error(L, "<getimage>: Index out of bounds (0,%d)", dets.size() - 1);
	if(size == 0)
		size = 256;
	if(padding == 0)
		padding = 0.5;

	full_object_detection shape = sp(img, dets[index]);
	array2d<rgb_pixel> img1;
	chip_details det = get_face_chip_details(shape, size, padding);
	if(shift)
	{
		det.rect.top() = det.rect.top() - det.rect.height() * shift;
		det.rect.bottom() = det.rect.bottom() - det.rect.height() * shift;
	}

	// crop the area of interest
	extract_image_chip(img, det, img1);
	int w = img1.nc();
	int h = img1.nr();

	// resize output tensor
	THFloatTensor_resize3d(t, 3, h, w);
	float *t_data = THFloatTensor_data(t);
	for(int y = 0; y < h; y++)
		for(int x = 0; x < w; x++)
		{
			*(t_data + y*t->stride[1] + x                 ) = img1[y][x].red / 255.0;
			*(t_data + y*t->stride[1] + x + t->stride[0]  ) = img1[y][x].green / 255.0;
			*(t_data + y*t->stride[1] + x + t->stride[0]*2) = img1[y][x].blue / 255.0;
		}
	// luaT_pushudata(L, (void *)t, "torch.FloatTensor");


	/* use this if we prefer to draw a polygon box around tilt face instead of a rectangle
	point p_center = center(shape.get_rect());
	point tl_raw = rotate_point(p_center, shape.get_rect().tl_corner(), det.angle);
	point bl_raw = rotate_point(p_center, shape.get_rect().bl_corner(), det.angle);
	point tr_raw = rotate_point(p_center, shape.get_rect().tr_corner(), det.angle);
	point br_raw = rotate_point(p_center, shape.get_rect().br_corner(), det.angle);
	*/


	// extract coordinates for features in table
	lua_newtable(L);
	lua_pushnumber(L, 0);
	lua_newtable(L);
	lua_pushliteral(L, "left");
	lua_pushnumber(L, floor(shape.get_rect().left()));
	lua_settable(L, -3);
	lua_pushliteral(L, "top");
	lua_pushnumber(L, floor(shape.get_rect().top()));
	lua_settable(L, -3);
	lua_pushliteral(L, "right");
	lua_pushnumber(L, floor(shape.get_rect().right()));
	lua_settable(L, -3);
	lua_pushliteral(L, "bottom");
	lua_pushnumber(L, floor(shape.get_rect().bottom()));
	lua_settable(L, -3);
	lua_settable(L, -3);

	for (int part = 0; part < (int) shape.num_parts(); part++) {
		lua_pushnumber(L, part + 1);
		lua_newtable(L);
		lua_pushliteral(L, "x");
		lua_pushnumber(L, shape.part(part).x());
		lua_settable(L, -3);
		lua_pushliteral(L, "y");
		lua_pushnumber(L, shape.part(part).y());
		lua_settable(L, -3);
		lua_settable(L, -3);
	}

	// return table
	return 1;
}

// Start tracking an object
// 1st parameter: image
// 2nd parameter: x coordinate of the top-left corner of the object to track
// 3rd parameter: y coordinate of the top-left corner of the object to track
// 4th parameter: width of the object to track
// 5th parameter: height of the object to track
// Returns: nothing

static int lua_trackstart(lua_State *L)
{
	luaimagetodlib(L, 1);
    tracker.start_track(img, centered_rect(point(lua_tointeger(L, 2), lua_tointeger(L, 3)),
		lua_tointeger(L, 4), lua_tointeger(L, 5)));
	return 0;
}

// Track the object
// 1st parameter: image
// Returns: x,y and width and height of the tracked object

static int lua_tracknext(lua_State *L)
{
	luaimagetodlib(L, 1);
	tracker.update(img);
	drectangle rect = tracker.get_position();
	lua_pushinteger(L, rect.left());
	lua_pushinteger(L, rect.top());
	lua_pushinteger(L, rect.width());
	lua_pushinteger(L, rect.height());
	return 4;
}

static const struct luaL_reg functions[] = {
	{"facealign", lua_facealign},
	{"loadmodel", lua_loadmodel},
	{"getimage", lua_getimage},
	{"trackstart", lua_trackstart},
	{"tracknext", lua_tracknext},
	{NULL, NULL}
};

// Initialize the library
extern "C" int luaopen_libface_align(lua_State * L)
{
	luaL_register(L, "libface_align", functions);
	return 1;
}

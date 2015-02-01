/*
 * Torch7 binding for FaceAlignment
 *
 * The main algorithm is presented by Xudong Cao et al.
 * in "Face Alignment by Explicit Shape Regression" (CVPR 2012)
 *
 * Author: Marko Vitez, Jonghoon Jin
 */
#include <luaT.h>
#include <TH/TH.h>
#include "FaceAlignment.h"
#include <exception>


using namespace cv;
using namespace std;
static ShapeRegressor regressor;
static bool initialized;


static bool facealign_file_exists(const char *filename)
{
   ifstream fin(filename);
   return fin.is_open();
}


static const char* facealign_check_binary_model(const char *path)
{
   //const char *path = lua_tostring(L, 1);
   char *ext = strrchr(path, '.');

   // input has no extension
   if (ext == NULL) {
      return path;

   // binary input
   } else if (strcmp (ext+1, "bin") == 0) {
      return path;

   // text input
   } else if (strcmp (ext+1, "txt") == 0) {
      char *path_bin = new char[200];
      int length = strlen(path);
      strcpy(path_bin, path);
      *(path_bin + --length) = 'n';
      *(path_bin + --length) = 'i';
      *(path_bin + --length) = 'b';

      if (!facealign_file_exists(path_bin)) {
         cout << "==> Create model binary file: " << path_bin << endl;
         ifstream file_txt(path);
         ofstream file_bin(path_bin, ios::binary);
         int d;
         while(file_txt >> d) {
            file_bin.write((char*) &d, sizeof(d));
         }
      }
      return (const char *) path_bin;

   // unknown input extension
   } else {
      return path;
   }
}


static int facealign_load(lua_State *L)
{
   const char *path = lua_tostring(L, 1);

   if(!path)
      luaL_error(L, "<loadmodel>: Missing model path");

   // const char *path_new = facealign_check_binary_model(path);

   // load pre-trained regressor model
   try {
      regressor.Load(path);
   } catch (exception &e) {
      luaL_error(L, "<loadmodel>: %s", e.what());
   }

   initialized = true;
   return 0;
}


static int facealign_align(lua_State *L)
{
   const char *tname = luaT_typename(L, 1);

   // check input argument
   if (!initialized)
      luaL_error(L, "<facealign>: Call load first");

   if (!tname)
      luaL_error(L, "<facealign>: Missing image");

   if (strcmp("torch.FloatTensor", tname))
      luaL_error(L, "<facealign>: cannot process tensor type %s (must be FloatTensor)", tname);

   // get image data
   THFloatTensor *frame =
      (THFloatTensor *)luaT_toudata(L, 1, luaT_typenameid(L, "torch.FloatTensor"));

   if (frame->nDimension != 3)
      luaL_error(L, "<facealign>: Unsupported tensor dimenstion %d (must be 3)", frame->nDimension);

   if (frame->stride[2] != 1)
      luaL_error(L, "<facealign>: Unsupported stride %d for 3rd dimension (must be 1)", frame->stride[2]);

   // access array and dimension info
   int c = frame->size[0];
   int h = frame->size[1];
   int w = frame->size[2];
   long stride_c = frame->stride[0];
   long stride_h = frame->stride[1];
   float *data = THFloatTensor_data(frame);

   // Build bounding box
   BoundingBox bb;
   bb.start_x = lua_tointeger(L, 2);
   bb.start_y = lua_tointeger(L, 3);
   bb.width = lua_tointeger(L, 4);
   bb.height = lua_tointeger(L, 5);
   bb.centroid_x = bb.start_x + bb.width/2.0;
   bb.centroid_y = bb.start_y + bb.height/2.0;

   // Build OpenCV Mat from image data
   Mat img[3];
   for (int i = 0; i < c; i++)
      img[i] = Mat(h, w, CV_32FC1, data+i*stride_c, 4*stride_h);

   // Convert a channel to byte for face-alignment
   Mat img_byte;
   img[0].convertTo(img_byte, CV_8UC1, 255.0);

   // Find fiducial points
   const int initial_number = 20;
   Mat_<double> current_shape = regressor.Predict(img_byte, bb, initial_number);

   // Grab points to estimate the size of new crop
   int xp1 = current_shape(0,0);   // leftmost point to estimate x_left
   int yp1 = current_shape(0,1);
   int xp2 = current_shape(1,0);   // rightmost point to estimate x_right
   int yp2 = current_shape(1,1);
   int xp5 = current_shape(4,0);   // top point1 to estimate y_top
   int yp5 = current_shape(4,1);
   int xp7 = current_shape(6,0);   // top point2 to estimate y_top
   int yp7 = current_shape(6,1);
   int xp29 = current_shape(28,0); // bottom point to estimate y_bottom
   int yp29 = current_shape(28,1);

   // Grab RELIABLE points to decide rotation reference
   int xp17 = current_shape(16,0); // left eye
   int yp17 = current_shape(16,1);
   int xp18 = current_shape(17,0); // right eye
   int yp18 = current_shape(17,1);
   int xp26 = current_shape(25,0); // between lips
   int yp26 = current_shape(25,1);

   // Calculate center of face (rotation axis)
   int xctr = (xp17 + xp18 + xp26)/3;
   int yctr = (yp17 + yp18 + yp26)/3;

   // Calculate rotation angle based on the slope between two eyes
   float angle = 180 / 3.1416 * atan2((float)(yp18-yp17),(float)(xp18-xp17));

   // Rotate the image at the center of face
   int len = max(w, h);
   Mat img_rotated[3];
   Mat r = getRotationMatrix2D(Point2f((float)xctr, (float)yctr), angle, 1.0);
   for (int i = 0; i < c; i++)
      warpAffine(img[i], img_rotated[i], r, Size(len, len));

   // Rotate boundary points to estimate size of crop
   int x_left  = r.at<double>(0, 0) * xp1 + r.at<double>(0, 1) * yp1 + r.at<double>(0,2);
   int x_right = r.at<double>(0, 0) * xp2 + r.at<double>(0, 1) * yp2 + r.at<double>(0,2);
   int yp5r    = r.at<double>(1, 0) * xp5 + r.at<double>(1, 1) * yp5 + r.at<double>(1,2);
   int yp7r    = r.at<double>(1, 0) * xp7 + r.at<double>(1, 1) * yp7 + r.at<double>(1,2);
   int y_top   = min(yp5r, yp7r);
   int y_bottom = r.at<double>(1, 0) * xp29 + r.at<double>(1, 1) * yp29 + r.at<double>(1,2);

   // Build destination storage for crop
   int w_crop = x_right - x_left;
   int h_crop = y_bottom - y_top;
   THFloatStorage *st = THFloatStorage_newWithSize(c*w_crop*h_crop);

   // Crop and paste to new storage
   for (int j = 0; j < c; j++) {
      for (int i = 0; i < h_crop; i++) {
         float *src = (float *)img_rotated[j].data + (y_top+i)*len + x_left;
         float *dst = (float *)st->data + j*h_crop*w_crop + i*w_crop;
         memcpy(dst, src, w_crop*sizeof(float));
      }
   }

   // Create tensor with cropped image
   THFloatTensor *t = THFloatTensor_newWithStorage3d(st, 0, c, w_crop*h_crop,
                                                     h_crop, w_crop, w_crop, 1);

   // return new tensor with aligned crop
   luaT_pushudata(L, (void *)t, "torch.FloatTensor");
   return 1;
}


static const struct luaL_reg functions[] = {
   {"process", facealign_align},
   {"load", facealign_load},
   {NULL, NULL}
};


extern "C" int luaopen_libface_align(lua_State * L)
{
   luaL_register(L, "libface_align", functions);
   return 1;
}

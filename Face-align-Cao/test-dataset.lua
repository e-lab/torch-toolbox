#!/usr/bin/env th
require('xlua')
require('sys')
require('image')
require('paths')
local FaceAlign = require('libface_align')
torch.setdefaulttensortype('torch.FloatTensor')


-- set initial bounding box ratio
local percent = 100

-- set src/dst paths
local src_root = '/path/to/your/dataset/'
local dst_root = src_root..'_aligned'..percent


-- set source images for face alignment
local src_train = src_root..'/train'
local src_test  = src_root..'/test'

-- set destination for aligned images
local dst_train = dst_root..'/train'
local dst_test  = dst_root..'/test'


local function apply_face_alignment(data_src, data_dst, percent)
   -- obtain list of categories
   local list_in_scope = sys.split(sys.ls(data_src), '\n')
   local percent = percent/100 or 1
   local cnt = 0

   -- for each category
   for i, cat in pairs(list_in_scope) do
      print('\n==> process category ', cat)
      local cat_no_space = string.gsub(cat, ' ', '\\ ')
      local cat_src_no_space = data_src..'/'..cat_no_space
      local cat_src = data_src..'/'..cat
      local cat_dst = data_dst..'/'..cat

      -- obtain list of images
      local list_images = sys.split(sys.ls(cat_src_no_space), '\n')

      -- create dst directory
      paths.mkdir(cat_dst)

      -- for each image in the category
      for j, fname in pairs(list_images) do
         xlua.progress(j, #list_images)
         local file_src = cat_src..'/'..fname
         local file_dst = cat_dst..'/'..fname

         -- load image
         local img_src = image.load(file_src)

         -- process image
         local w = img_src:size(3)
         local h = img_src:size(2)
         local dw = math.floor((1 - percent)/2*w + 0.5)
         local dh = math.floor((1 - percent)/2*h + 0.5)
         local bb = {
            x = math.max(1, 1+dw),
            y = math.max(1, 1+dh),
            w = math.min(w, w-dw),
            h = math.min(h, h-dh),
         }
         local img_dst = FaceAlign.process(img_src, bb.x, bb.y, bb.w, bb.h)

         -- save image
         image.save(file_dst, img_dst)
         cnt = cnt + 1
      end
   end
   return cnt
end


-- load regressor model parameters
-- URL: https://docs.google.com/uc?export=download&confirm=G1sQ&id=0B0tUTCaZBkccOGZTcjJNcDMwa28
FaceAlign.load('model.txt')

-- apply face-align on training set
local nb_train = apply_face_alignment(src_train, dst_train, percent)
print('==> #training image', nb_train)

-- apply face-align on testing set
local nb_test = apply_face_alignment(src_test, dst_test, percent)
print('==> #testing image', nb_test)

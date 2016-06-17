require 'image'
local videodecoder = require 'libvideo_decoder'
local ffi = require('ffi')
ffi.cdef [[
   int connectedComponent(int *outputMaskPointer, int *coordinates, int coordinatesSize, int height, int width);
]]
local lib8cc = ffi.load('lib8cc.so')
local process = {}
local diffarea, diffsens
local previmg
local blobs
local motionmode
local fimg

-- Used by getmotionblobs
local motion_reduce = 8
local motion_maxblobs = 10
local motion_meansize = 2
local diffbuffer
local motion_count
local blobdata = torch.IntTensor(motion_maxblobs, 4):zero() -- coordinates of blobs

-- Crop a batch of images to croprect, reuse the cropimg tensor
local function cropbatch(img, croprect, cropimg)

   if cropimg == nil or cropimg:size(1) ~= img:size(1) then
      local side = math.min(croprect.x2 - croprect.x1, croprect.y2 - croprect.y1)
      cropimg = torch.Tensor(img:size(1), img:size(2), side, side)
   end
   for i=1,img:size(1) do
      cropimg[i] = image.crop(img[i], croprect.x1, croprect.y1, croprect.x2, croprect.y2)
   end
   return cropimg

end

-- Scale a batch of images to shortest side = eye and normalize, reuse the scaledimg tensor
local function scalebatch(img, eye, stat, scaledimg)

   local w, h
   if img:size(3) < img:size(4) then
      w = eye * img:size(4) / img:size(3)
      h = eye
   else
      w = eye
      h = eye * img:size(3) / img:size(4)
   end
   if scaledimg == nil or scaledimg:size(1) ~= img:size(1) then
      scaledimg = torch.Tensor(img:size(1), img:size(2), h, w)
   end
   for i = 1, img:size(1) do
      if img[i]:size(1) == 3 and img[i]:size(2) == h and img[i]:size(3) == w then
         scaledimg[i] = img[i]   -- don't scale if the size is already correct
      else
         scaledimg[i] = image.scale(img[i], w, h)
      end
      for c = 1,3 do
         scaledimg[i][c]:add(-stat.mean[c])
         scaledimg[i][c]:div(stat.std [c])
      end
   end
   return scaledimg

end

-- Normalize a batch of images
local function normalizebatch(img, stat)

   for i = 1, img:size(1) do
      for c = 1,3 do
         img[i][c]:add(-stat.mean[c])
         img[i][c]:div(stat.std [c])
      end
   end

end

--
local function motionsensed(img)

   if not diffsens or diffsens == 0 or motionmode then
      return true
   else
      img = img:dim() == 4 and img[1] or img
      img = image.convolve(img, image.gaussian(3), 'same')
      local diff
      if previmg then
         videodecoder.loglevel(5)
         diff = videodecoder.diffimages(previmg, img, diffsens, diffarea)
      else
         diff = true;
      end
      previmg = img:clone()
      return diff
   end

end

-- Expand the rectangle to a square, which has to stay inside the frame
local function expandrect(rect, h, w)

   -- Expand the rectangle into a square
   if rect.x2-rect.x1 < rect.y2-rect.y1 then
      rect.x1 = rect.x1 - (rect.h - rect.w) / 2
      rect.w = rect.h
   else
      rect.y1 = rect.y1 - (rect.w - rect.h) / 2
      rect.h = rect.w
   end
   -- Shrink the square if it's higher of the frame
   if rect.h > h then
      rect.x1 = rect.x1 + (rect.h - h) / 2
      rect.y1 = rect.y1 + (rect.h - h) / 2
      rect.w = h
      rect.h = h
   end
   -- Move the square inside the frame
   if rect.x1 + rect.w > w then
      rect.x1 = w - rect.w
   elseif rect.x1 < 0 then
      rect.x1 = 0
   end
   if rect.y1 + rect.h > h then
      rect.y1 = h - rect.h
   elseif rect.y1 < 0 then
      rect.y1 = 0
   end
   rect.x2 = rect.x1 + rect.w
   rect.y2 = rect.y1 + rect.w

   return rect

end

-- Get motion blobs from img as a Tensor of rectangles
-- It requires two external variables: diffbuffer to keep the history
-- and previmg to keep the previous image
local function getmotionblobs(img)

   local h = img:size(2)
   local w = img:size(3)
   img = image.convolve(img, image.gaussian(3), 'same')
   -- Compute difference of the images and reduce them in size
   previmg = previmg or img
   local diff = image.rgb2y(image.scale(previmg:add(-img):abs(), w/motion_reduce, h/motion_reduce, 'simple'))
   previmg = img
   -- Find different pixels
   diffbuffer = diffbuffer or torch.zeros(motion_meansize, h/motion_reduce, w/motion_reduce)
   diffbuffer[motion_count % motion_meansize + 1] = diff:gt(diffsens)
   motion_count = motion_count + 1
   local meandiff = diffbuffer:sum(1)
   -- Dilate and erode
   meandiff = image.dilate(meandiff[1], torch.ones(9,9))
   meandiff = image.erode(meandiff, torch.ones(5,5))
   -- Pad
   local padded = torch.IntTensor(meandiff:size(1) + 2, meandiff:size(2) + 2):zero()
   padded[{ {2, meandiff:size(1) + 1}, {2, meandiff:size(2) + 1} }] = meandiff
   local nblobs = lib8cc.connectedComponent(torch.data(padded), torch.data(blobdata), motion_maxblobs, padded:size(1), padded:size(2))
   if nblobs > 0 then
      -- Return coordinates in the img scale from 0 to size
      local blobs = blobdata[{{1,nblobs}}]
      -- Subtract 1 only from the left-top origin, so bottom-right will be outside the rect
      -- and their difference will be the rectangle size
      blobs[{ {}, {1,2} }]:add(-1)
      return blobs * motion_reduce
   else
      return nil
   end

end

--[[

   opt fields
      is          eye size
      platform    cpu or cuda
      spatial     spatial mode 0, 1 or 2
      diffarea    between 0 and 1, amount of pixels to be different
      diffsens    between 0 and 1, min difference in luminance
                  to consider it different; 0 means no such processing

   source fields
      w           image width
      h           image height

   network fields
      model       model
      stat        stat.mean and stat.std

--]]
function process:init(opt, source, network)

   local eye = opt.is
   local scaledimg = nil
   local cropimg = nil
   local result = nil
   local cuda = false
   local croprect = {}

   previmg = nil
   diffbuffer = nil
   motion_count = 0
   blobs = nil
   diffarea = opt.diffarea
   diffsens = opt.diffsens
   motionmode = opt.spatial == 0 and diffsens > 0
   if opt.spatial == 0 then
      local minside = math.min(source.h, source.w)
      croprect.x1  = source.w / 2 - minside / 2
      croprect.y1  = source.h / 2 - minside / 2
      croprect.x2  = source.w / 2 + minside / 2
      croprect.y2  = source.h / 2 + minside / 2
   elseif opt.spatial == 1 or opt.spatial == 2 then
      local spatial = assert(require('spatial'))
      network.model = spatial:init(opt, network.model, source)
   else
      error('Spatial mode must be 0, 1 and 2')
   end

   if opt.platform == 'cuda' then
      assert(require('cunn'))
      cuda = true
      network.model:cuda()
   elseif opt.platform ~= 'cpu' then
      error('Platform can be cpu or cuda')
   end

   process.forward = function(img)

      if img:dim() == 3 then  -- Make 4D, if not
         img = img:view(1, img:size(1), img:size(2), img:size(3))
      end
      if motionsensed(img) then
         if opt.spatial == 2 or motionmode then
            normalizebatch(img, network.stat)
         elseif opt.spatial == 0 then
            cropimg = cropbatch(img, croprect, cropimg)
            scaledimg = scalebatch(cropimg, eye, network.stat, scaledimg)
            img = scaledimg
         else -- opt.spatial == 1
            scaledimg = scalebatch(img, eye, network.stat, scaledimg)
            img = scaledimg
         end
         if cuda then
            result = network.model:forward(img:cuda()):float()
         elseif img:size(1) == 1 then
            result = network.model:forward(img:squeeze(1)) -- OpenBLAS has issues with batches on ARM
         else
            result = network.model:forward(img)
         end
         -- Make 4D again
         if result:dim() == 1 then
            result = result:view(1, result:size(1), 1, 1)
         elseif result:dim() == 2 then
            result = result:view(result:size(1), result:size(2), 1, 1)
         elseif result:dim() == 3 then
            result = result:view(1, result:size(1), result:size(2), result:size(3))
         end
      end
      if opt.spatial == 0 and not motionmode then
         return result, cropimg
      else
         return result, img
      end

   end --process.forward

   process.motionframe = function(img, frame)

      while true do
         while not blobs do
            fimg = frame.forward(img)
            if not fimg then
               return nil
            end
            if fimg:dim() == 4 then
               fimg = fimg[1]
            end
            assert(fimg:dim() == 3)  -- This does not work on batches
            blobs = getmotionblobs(fimg)
            curblob = 1
         end
         local rect = {}
         rect.x1 = blobs[curblob][1]
         rect.y1 = blobs[curblob][2]
         rect.x2 = blobs[curblob][3]
         rect.y2 = blobs[curblob][4]
         rect.w = rect.x2 - rect.x1
         rect.h = rect.y2 - rect.y1
         curblob = curblob + 1
         if curblob > blobs:size(1) then
            blobs = nil
         end
         if math.min(rect.w, rect.h) > eye / 4 and rect.w < fimg:size(3) * 9/10 and rect.h < fimg:size(2) then
            -- Only consider blobks that are bigger of 1/4 of the eye size and smaller of the entire image
            -- Create a square which has to stay inside the frame
            rect = expandrect(rect, fimg:size(2), fimg:size(3), eye)
            local croppedimg = fimg[{ {}, {rect.y1+1, rect.y2}, {rect.x1+1, rect.x2} }]
            return image.scale(croppedimg, eye, eye)
         end
      end

   end -- process.motionframe

end

return process

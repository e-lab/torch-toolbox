--[[ frame

The 'frame' package has two public functions. The first is the 'init' function
that takes as arguments the application options table and a second source
definitions table. The second public function is a 'forward' function that is
created by the the 'init' function using the options/source tables to select an
appropriate method. It is this 'forward' function that is called by the
applications 'main' loop to grab an image frame.

--]]
local frame = {}
torch.setdefaulttensortype('torch.FloatTensor')

local sys = assert(require('sys'))

local pf = function(...) print(string.format(...)) end
local Cr = sys.COLORS.red
local Cb = sys.COLORS.blue
local Cg = sys.COLORS.green
local Cn = sys.COLORS.none
local THIS = sys.COLORS.blue .. 'THIS' .. Cn

local function prep_lua_camera(opt, source)
   assert(require('camera'))

   local cam = image.Camera {
      idx      = opt.cam,
      width    = source.w,
      height   = source.h,
   }

   -- set frame forward function
   frame.forward = function(img)
      return cam:forward(img)
   end

   -- load postfile routine from libvideo_decoder library:
   source.cam = cam
end


local function prep_libcamera_tensor(opt, source)
   local cam = assert(require("libcamera_tensor"))

   if not cam.init(opt.cam, source.w, source.h, opt.fps, 1) then
      error("No camera")
   end

   -- set frame forward function
   frame.forward = function(img)
      if not cam.frame_rgb(opt.cam, img) then
         error('frame grab failed')
      end

      return img
   end

   source.cam = cam
end


local function prep_libvideo_decoder_camera(opt, source)
   local cam = vid

   local status = false
   if opt.wthumbs ~= 2 and not opt.fastpreview then
      status = cam.capture('/dev/video'..opt.cam, source.w, source.h, source.fps, 3)
      if not status then
         error('cam.capture failed')
      end
   else
      -- if we want to stream detections to server:
      if opt.wthumbs == 2 then
         status = cam.capture('/dev/video'..opt.cam, source.w, source.h, source.fps, 16, 'auto', 25)
      else
         status = cam.capture('/dev/video'..opt.cam, source.w, source.h, source.fps, 16, '', 25)
      end
      if not status then
         error('cam.capture failed')
      end

      status = cam.startremux(mediadir..'/video', 'mp4')
      if status == 0 then
         error('cam.startremux failed')
      end
   end

   if opt.spatial == 1 then
      source.w = opt.is * source.w / source.h
      source.h = opt.is
      framefunc = cam.frame_resized
   else
      framefunc = cam.frame_rgb
   end
   local img_tmp = torch.ByteTensor(3, source.h, source.w)

   -- set frame forward function
   frame.forward = function(img)
      if not framefunc(img_tmp) then
         return false
      end
      return img_tmp
   end

   source.cam = cam
end

local function prep_livecam(opt, source)
   local cam = opt.input == 'geocam' and require 'geolivecam' or require 'livecam'

   local status = false
   cam.capture('/dev/video'..opt.cam, source.w, source.h, source.fps)
   local img_tmp = torch.FloatTensor(3, source.h, source.w)

   -- set frame forward function
   frame.forward = function(img)
      cam.frame_rgb(img_tmp)
      return img_tmp
   end

   source.cam = cam
end


local function prep_libvideo_decoder_video(opt, source)
   local cam = vid

   local status = false
   status, source.h, source.w, source.length, source.fps = cam.init(opt.input);
   if not status then
      error("No video")
   else
      if opt.loglevel > 0 then
         pf(Cb..'video statistics: %s fps, %dx%d (%s frames)'..Cn,
            (source.fps and tostring(source.fps) or 'unknown'),
            source.h,
            source.w,
            (source.length and tostring(source.length) or 'unknown'))
      end
   end

   -- remux video if we want to save video segments
   if opt.wthumbs == 2 then
      if opt.gui then
         status = cam.startremux('video.mp4', 'mp4')
         if status == 0 then
            error('cam.startremux failed')
         end
      else
         status = cam.startremux(mediadir..'/video', 'mp4')
         if status == 0 then
            error('cam.startremux failed')
         end
      end
   end

   if opt.spatial == 1 then
      source.w = opt.is * source.w / source.h
      source.h = opt.is
      framefunc = cam.frame_resized
   else
      framefunc = cam.frame_rgb
   end
   local img_tmp = torch.ByteTensor(opt.batch, 3, source.h, source.w)

   -- set frame forward function
   frame.forward = function(img)
      local n = opt.batch
      for i=1,opt.batch do
         if not framefunc(img_tmp[i]) then
            if i == 1 then
               return false
            end
            n = i-1
            break
         end
      end
      if n == opt.batch then
         img = img_tmp
      else
         img = img_tmp:narrow(1,1,n)
      end

      return img
   end

   source.cam = cam
end


function frame:init(opt, source)

   if string.sub(opt.input, 1, 3)  == 'cam' and tonumber(string.sub(opt.input,4,-1)) ~= nil then

      opt.cam = tonumber(string.sub(opt.input,4,-1)) -- opt.input is in the format cam0

      if (sys.OS == 'macos') then

         prep_lua_camera(opt, source)

      elseif (opt.platform == 'nnx') then

         prep_libcamera_tensor(opt, source)

      elseif opt.livecam then

         prep_livecam(opt, source)

      else

         prep_libvideo_decoder_camera(opt, source)

      end

   elseif opt.input == 'geocam' then

      opt.livecam = true
      prep_livecam(opt, source)

   else

      prep_libvideo_decoder_video(opt, source)

   end
end


return frame

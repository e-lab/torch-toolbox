#!/usr/bin/env qlua
--
-- Testing script for FaceAlignment Torch7 binding
--
-- Author: Marko Vitez, Jonghoon Jin
--
require('image')
local FaceAlign = assert(require('libface_align'))
torch.setdefaulttensortype('torch.FloatTensor')


-- on MAC, video_decoder capture routine does not work
local on_MAC = (paths.is_mac() == 'Darwin')


print('==> Loading model')
-- URL: http://sourceforge.net/projects/dclib/files/dlib/v18.10/shape_predictor_68_face_landmarks.dat.bz2
FaceAlign.loadmodel('shape_predictor_68_face_landmarks.dat')


local img
local width
local height


-- init camera/video driver
print('==> Starting to capture')
if on_MAC then
   require('camera')
   cam = image.Camera{width=640, height=360}
   img = cam:forward()
   width = img:size(3)
   height = img:size(2)
else
   video = assert(require("libvideo_decoder"))
   width = 640 
   height = 360
   local status = video.capture('/dev/video0', width, height, 10)
   if not status then
      error("No video")
   end
   img = torch.FloatTensor(3, height, width)
end


local win1, win2
while true do
   -- grab frame
   if on_MAC then
      img = cam:forward()
   else
      if not video.frame_rgb(img) then
         error('frame grab failed')
      end
   end
   win1 = image.display{image=img, win=win1}

   -- align image
   nimages = FaceAlign.facealign(img)
   print('nimages='..nimages)

   -- display result
   if nimages >= 1 then
      local img1 = torch.Tensor()
      FaceAlign.getimage(img1, 0)
      win2 = image.display{image=img1, win=win2}
   end
end


-- close video_decoder if initialized
if not on_MAC then
   video.exit()
end

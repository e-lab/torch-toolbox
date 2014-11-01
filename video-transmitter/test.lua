--------------------------------------------------------------------------------
-- transmission of a video via UDP for torch7
--------------------------------------------------------------------------------
-- Jonghoon, Jin, E. Culurciello, October 31, 2014
--------------------------------------------------------------------------------

require("pl")
require("image")
local video = assert(require("libvideo_transmitter"))

-- Options ---------------------------------------------------------------------
opt = lapp([[
-v, --videoPath    (default '')    path to video file
]])


-- set streaming configuration
local width = 512
local height = 512
local bar = math.floor(height/4)
local img_raw = image.scale(image.lena():mul(255):byte(), width, height)
local img = img_raw:clone()
local nb_frames = 100000
local fps = 25
local quality = 3
local url = 'udp://'..(opt.videoPath or assert(false))..':6970'

-- initialize transmitter and allocate buffers
video.init(img, url, fps, quality)

-- looping the lena for 'n' times
local timer = torch.Timer()
for i = 1, nb_frames do
   local c = math.fmod(i,width)+1
   if c == 1 then
      img = img_raw:clone()
   end

   -- draw nice bars to show progress
   img[{1, {1*bar, 1*bar+20}, c}]:fill(0)
   img[{2, {2*bar, 2*bar+20}, c}]:fill(0)
   img[{3, {3*bar, 3*bar+20}, c}]:fill(0)

   -- trasmit image via UDP
   video.forward()
end
print('Time: ', timer:time().real/nb_frames)

-- free variables and close the streamer
video.close()

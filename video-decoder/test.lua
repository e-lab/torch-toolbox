--------------------------------------------------------------------------------
-- loading of a video for torch7
--------------------------------------------------------------------------------
-- Jonghoon, Jin, E. Culurciello, October 9, 2014
--------------------------------------------------------------------------------

require("pl")
require("image")
local video = assert(require("libvideo_decoder"))

-- Options ---------------------------------------------------------------------
opt = lapp([[
-v, --videoPath    (default '')    path to video file
]])


-- load a video and extract frame dimensions
local status, height, width, length, fps = video.init(opt.videoPath)
if not status then
   error("No video")
else
   print('Video statistics: '..height..'x'..width..' ('..(length or 'unknown')..' frames)')
end

-- construct tensor
local dst = torch.ByteTensor(3, height, width)

-- looping the video for 'length' times
local win
local nb_frames = length or 10
local timer = torch.Timer()
for i = 0, nb_frames do
   video.frame_rgb(dst)
   win = image.display{image = dst, win = win}
end
print('Time: ', timer:time().real/nb_frames)

-- free variables and close the video
video.exit()

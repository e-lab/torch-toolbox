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

-- play and save a stream
video.startrx("video.mp4", "mp4")
local win
local timer = torch.Timer()
while(true) do
   video.frame_rgb(dst)
   win = image.display{image = dst, win = win}
end
print('Time: ', timer:time().real/nb_frames)
video.stoprx()

-- free variables and close the video
video.exit()

local frame = {}
torch.setdefaulttensortype('torch.FloatTensor')

local pf = function(...) print(string.format(...)) end
local Cb = sys.COLORS.blue
local Cn = sys.COLORS.none

--[[
   opt fields
      is          eye size
      pyramid     true if pyramid mode
      input       filename
      batch       batch size
      loglevel    if w

   source fields
      w           image width
      h           image height

   in pyramid mode it fills source.resolutions with the resolutions that will be returned
   (indexed by res_idx)
--]]
function frame:init(opt, source)

   local vd = require('libvideo_decoder')
   local status = false
   local width = source.w
   status, source.h, source.w, source.length, source.fps = vd.init(opt.input);
   source.origw = source.w
   source.origh = source.h
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
   if opt.pyramid then
      source.resolutions = {}
      local i = 1
      source.h = math.floor(width * source.h / source.w + 0.5)
      source.w = width
      local nextsize = {source.w, source.h}
      repeat
         source.resolutions[i] = nextsize
         nextsize = {math.floor(nextsize[1] * 0.707 + 0.5), math.floor(nextsize[2] * 0.707 + 0.5)}
         i = i+1
      until nextsize[1] < opt.is or nextsize[2] < opt.is
   end

   if opt.spatial == 1 then -- In spatial mode 1 we can already get the scaled image
      source.w = opt.is * source.w / source.h
      source.h = opt.is
      framefunc = vd.frame_resized
   else
      framefunc = vd.frame_rgb
   end
   local img_tmp = torch.FloatTensor(opt.batch, 3, source.h, source.w)

   if pyramid then
      frame.forward = function(img, res_idx)
         return vd.frame_batch_resized(opt.batch, source.resolutions[res_idx][1], source.resolutions[res_idx][2], res_idx == 1)
      end
   else
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
   end

end

return frame

local frame = {}
torch.setdefaulttensortype('torch.FloatTensor')

--[[
   opt fields
      is          eye size
      pyramid     true if pyramid mode
      input       filename
      batch       batch size

   source fields
      w           image width
      h           image height

   in pyramid mode it fills source.resolutions with the resolutions that will be returned
   (indexed by res_idx)
--]]
function frame:init(opt, source)

   local fi = require('fastimage')
   local batch = opt.batch
   local filenames
   local first = true
   local resolutions = nil
   if opt.pyramid then
      source.resolutions = {}
      local i = 1
      local nextsize = {source.w, source.h}
      repeat
         source.resolutions[i] = nextsize
         nextsize = {math.floor(nextsize[1] * 0.707 + 0.5), math.floor(nextsize[2] * 0.707 + 0.5)}
         i = i+1
      until nextsize[1] < opt.is or nextsize[2] < opt.is
      fi.init(opt.input, opt.batch, torch.Tensor(source.resolutions), 0.5)
   else
      fi.init(opt.input, opt.batch, 0, 0, 0.5)
   end
   source.img, filenames = fi.load(nil)
   if source.img then
      source.w = source.img:size(4)
      source.origw = source.w
      source.h = source.img:size(3)
      source.origh = source.h
   end
   if batch == 1 and source.img then
      source.img = source.img[1]
      filenames = filenames[1]
   end

   frame.forward = function(img, res_idx)
      if first then
         first = false
         return source.img, filenames
      else
         source.img, filenames = fi.load(img, res_idx)
         if batch == 1 and source.img then
            source.img = source.img[1]
            filenames = filenames[1]
         end
         return source.img, filenames
      end
   end

end

return frame

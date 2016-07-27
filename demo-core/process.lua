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
local bminsize = 64 -- math.max(source.w, source.h)*1/10 -- minimum blob size
local decayLimit = 3     -- # of frames an object will keep on shown even if it is not detected
local minStillFrames = 3 -- # of frames an object has to remain still, until it is actually tagged as still object
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
      if stat and stat.mean and stat.std then
         for c = 1,3 do
            scaledimg[i][c]:add(-stat.mean[c])
            scaledimg[i][c]:div(stat.std[c])
         end
      end
   end
   return scaledimg

end

-- Normalize a batch of images
local function normalizebatch(img, stat)

   if stat and stat.mean and stat.std then
      for i = 1, img:size(1) do
         for c = 1,3 do
            img[i][c]:add(-stat.mean[c])
            img[i][c]:div(stat.std[c])
         end
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
local function expandrect(origrect, h, w)

   local rect = {}
   -- Expand the rectangle into a square
   if origrect.x2-origrect.x1 < origrect.y2-origrect.y1 then
      rect.x1 = origrect.x1 - (origrect.h - origrect.w) / 2
      rect.y1 = origrect.y1
      rect.w = origrect.h
      rect.h = origrect.h
   else
      rect.x1 = origrect.x1
      rect.y1 = origrect.y1 - (origrect.w - origrect.h) / 2
      rect.w = origrect.w
      rect.h = origrect.w
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
local function getmotionblobs(img, cam)

   local h = img:size(2)
   local w = img:size(3)
   local diff
   img = image.convolve(img, image.gaussian(3), 'same')
   -- Compute difference of the images and reduce them in size
   if cam then
      yimg = yimg or torch.ByteTensor(h/motion_reduce, w/motion_reduce)
      cam.frame_y(yimg)
      previmg = previmg or yimg:clone()
      diff = previmg:add(-yimg)
      diff:apply(function(x) return (x < 128 and x or 256 - x) end)
      diff = diff:float() / 256
      previmg = yimg:clone()
   else
      previmg = previmg or img:clone()
      diff = image.rgb2y(image.scale(previmg:add(-img):abs(), w/motion_reduce, h/motion_reduce, 'simple'))
      previmg = img
   end
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

local function applynetwork(img, rect, network, eye)

   local croprect = expandrect(rect, img:size(2), img:size(3))
   local cropimg = image.crop(img, croprect.x1, croprect.y1, croprect.x2, croprect.y2)
   local scaledimg = image.scale(cropimg, eye, eye)
   if network.mean and network.std then
      for c = 1,3 do
         scaledimg[c]:add(-network.mean[c])
         scaledimg[c]:div(network.std[c])
      end
   end
   local prob = network.net:forward(scaledimg)
   return prob:squeeze(), scaledimg

end

local function fixedcam_forward(img, network, eye)

   img = img[1]
   local blobs = getmotionblobs(img)
   if blobs then
      for i=1,blobs:size() do
         local croprect = expandrect(blobs[i], img:size(3), img:size(4))
         local cropimg = image.crop(img, croprect.x1, croprect.y1, croprect.x2, croprect.y2)
         local scaledimg = image.scale(cropimg, eye, eye)
         local prob = network.net:forward(scaledimg)
         updateobjects(prob, blobs[i])
      end
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
      mean        mean
      std         std

--]]
function process:init(opt, source, network)

   local eye = opt.is
   local scaledimg = nil
   local cropimg = nil
   local result = nil
   local cuda = false
   local croprect = {}
   local personCatNum
   local objectID = 0
   local itercnt = 0
   local objects = {}
   local livecam = opt.livecam and require 'livecam'

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
      network.net = spatial:init(opt, network.net, source)
   else
      error('Spatial mode must be 0, 1 and 2')
   end

   for i=1,#network.labels do
      if network.labels[i] == "person" then
         personCatNum = i
      end
   end

   if opt.platform == 'cuda' then
      assert(require('cunn'))
      cuda = true
      network.net:cuda()
   elseif opt.platform ~= 'cpu' then
      error('Platform can be cpu or cuda')
   end

   process.forward = function(img)

      if img:dim() == 3 then  -- Make 4D, if not
         img = img:view(1, img:size(1), img:size(2), img:size(3))
      end
      if motionsensed(img) then
         if opt.spatial == 2 or motionmode then
            normalizebatch(img, network)
         elseif opt.spatial == 0 then
            cropimg = cropbatch(img, croprect, cropimg)
            scaledimg = scalebatch(cropimg, eye, network, scaledimg)
            img = scaledimg
         else -- opt.spatial == 1
            scaledimg = scalebatch(img, eye, network, scaledimg)
            img = scaledimg
         end
         if opt.fixedcam then
            -- Very special case, completely different workflow
            return fixedcam_forward(img, network, opt.eye)
         elseif cuda then
            result = network.net:forward(img:cuda()):float()
         elseif img:size(1) == 1 then
            result = network.net:forward(img:squeeze(1)) -- OpenBLAS has issues with batches on ARM
         else
            result = network.net:forward(img)
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
            -- Only consider blobs that are bigger of 1/4 of the eye size and smaller of the entire image
            -- Create a square which has to stay inside the frame
            rect = expandrect(rect, fimg:size(2), fimg:size(3))
            local croppedimg = fimg[{ {}, {rect.y1+1, rect.y2}, {rect.x1+1, rect.x2} }]
            return image.scale(croppedimg, eye, eye)
         end
      end

   end -- process.motionframe

   process.trackerforward = function(img)

      itercnt = itercnt + 1 -- frame count

      blobs = getmotionblobs(img, livecam) -- saliency algorithm
      if blobs ~= nil then

         -- once we have motion, we find 'objects' which are targets of interest (tracks + data)
         for i=1, blobs:size(1) do
            local d = {}
            d.x1 = blobs[i][1]
            d.y1 = blobs[i][2]
            d.x2 = blobs[i][3]
            d.y2 = blobs[i][4]
            d.w = d.x2 - d.x1
            d.h = d.y2 - d.y1
            if d.w > bminsize and d.h > bminsize then -- the image needs to have a reasonable size

               prevresult, scld = applynetwork(img, d, network, eye)

               -- iterate on all found objects to update coordinates:
               local b2o = 0 -- blobs belongs to object b2o (if 0 belongs to no object()
               for j, o in ipairs(objects) do
                  --print("cx "..d.cx.." c.cx "..o.coor.cx)
                  --print("cy "..d.cy.." c.cy "..o.coor.cy)
                  --print("dx "..math.max(o.coor.w, d.w).." dy "..math.max(o.coor.h, d.h))

                  -- check if new blob is part of another object / overlaps
                  -- overlap variable gives the % overlap of existing object with current blob
                  local x1 = math.max(d.x1, o.coor.x1)
                  local x2 = math.min(d.x2, o.coor.x2)
                  local y1 = math.max(d.y1, o.coor.y1)
                  local y2 = math.min(d.y2, o.coor.y2)

                  minObjArea = math.min(d.w * d.h, o.coor.w * o.coor.h)
                  overlap = (math.abs(y2 - y1) * math.abs(x2 - x1))/minObjArea
                  if overlap > 0.5 and (y2 - y1) > 0 and (x2 - x1) > 0 then
                     if b2o ~= 0 then
                        table.remove(objects,j)
                     else
                        b2o = j
                        -- When there is sudden DECREASE in bounding box area,
                        -- most probably it is about to become still and only
                        -- some parts (hands, head, etc.) are in motion.
                        local newArea = d.w * d.h
                        local oldArea = o.coor.w * o.coor.h
                        if (oldArea/newArea) > 2 then
                           d = o.coor
                        end
                        -- print("Previous object")
                     end
                  end
               end

               -- if object of interest(eg.: "person") found, then
               -- we create 'objects' out of salient blobs
               -- then perform face-detection and tracking on motion blobs:

               local target = ( prevresult[personCatNum] >= opt.pdt ) -- above detection threshold or not?
               local newObject = ( b2o == 0 )
               local oc = objects[b2o] -- shortcut!
               -- print('Blob processing:',i,target,newObject,b2o,#objects)

               -- case 1: new target object (large bounding box)
               -- case 2: new object candidate (thin gray bounding box)
               -- case 3: convert candidate to object / update position of target object
               -- case 4: update position of target candidate

               if target and newObject then  -- case 1
                  -- print('case1')
                  local new = {}
                  new.id = objectID + 1 -- id of object
                  new.target = true
                  new.class = personCatNum
                  new.detections = 3 -- detection of target
                  new.results = prevresult
                  new.coor = d
                  new.crop = scld
                  new.valid = true
                  new.decay = decayLimit
                  new.stillObj = false
                  new.permaStill = 0
                  objectID = objectID + 1
                  table.insert(objects, new)

               elseif not target and newObject then -- case 2
                  -- print('case2')
                  local new = {}
                  new.id = objectID + 1
                  new.target = false
                  new.coor = d
                  new.valid = true
                  new.stillObj = false
                  new.decay = decayLimit
                  new.detections = 0
                  new.permaStill = 0
                  table.insert(objects, new)

               elseif target and not newObject then -- case 3
                  -- print('case3')
                  if oc.stillObj == true then
                     oc.stillObj = true
                     oc.valid = false
                  else
                     oc.coor = d
                     oc.valid = true
                  end
                  oc.decay = decayLimit
                  if oc.target then -- target detected one more time!
                     oc.detections = 3
                     -- print('Target detections #', oc.detections)
                     -- if we are tracking the object we want the tracker to overwrite coordinates!
                     -- if oc.tracked then trackNext(img[1], oc) end
                  else -- candidate upgraded to object
                     oc.id = objectID + 1 -- id of object
                     objectID = objectID + 1
                     oc.target = true
                     oc.class = personCatNum
                     oc.detections = 3
                  end
                  oc.results = prevresult
                  oc.crop = scld
                  --print(oc.detections)
               elseif not target and not newObject then -- case 4
                  -- print('case4')
                   if oc.stillObj == true then
                     oc.stillObj = true
                     oc.valid = false
                  else
                     oc.coor = d
                     oc.valid = true
                  end
                  oc.decay = decayLimit
                  --print(oc.detections)
               end
            end -- check blob size
         end
      end

      --[[
      Different fields in object
      ========================================================================================
      id:        Object id to be displayed
      crop:      Region that can be sent to NN (231x231)
      target:    Blob is object of interest or not (currently person)
      class:     Object name + id
      detection: # of detections
      results:
      coor:      Coordinates of bounding box
      valid:     Accounts for dissapearance of previously detected object/blob
      stillObj:  Track if a previously detected object became still and is not moving anymore
      permaStill:Count for how many frames the object has been still
      ========================================================================================
      --]]

      -- Reject objects that have disappeared. In this case objects which are not assigned as valid
      -- in the current frame have stopped moving or are out of the frame.

      for j, o in ipairs(objects) do
         -- When there is no motion and hence no blob detected, o.valid is false.
         -- So, in case of still object or disappeared object, o.valid = false
         -- to ensure this o.valid is set to true only when a blob is found
         if o.valid == true then          -- Detected moving blobs
            o.valid = false
         else
            -- print("Detecting still object ",j)
            -- print(o)
            prevresult = applynetwork(img, o.coor, network, eye)

            local target = ( prevresult[personCatNum] >= opt.pdt ) -- above detection threshold or not?
            -- o.results = prevresult
            -- o.class = personCatNum
            -- o.valid = false
            -- o.crop = scld
            -- When an object becomes still, we only need to retain it if it is a target
            -- Consider an object to be still only when it has been still for more than a certain # of frames
            if target then
               if o.permaStill > minStillFrames then
                  o.stillObj = true
               else
                  o.stillObj = false
               end
            else
               -- When the object from previous image is not found in the current image, it means no target found.
               -- It means it has gone out from the image and so wee need to delete info about it from our object table.
               if o.decay == 0 then
                  table.remove(objects,j)
               else
                  o.decay = o.decay - 1
               end
            end
            o.permaStill = o.permaStill + 1
         end

         -- for every object: find faces and track:
         if opt.faceid then
            --faceProcess(o)
         end
      end
      return nil, img:view(1,img:size(1), img:size(2), img:size(3)), objects
   end --process.trackerforward

   if opt.fixedcam then
      process.forward = process.trackerforward
   end

end

return process

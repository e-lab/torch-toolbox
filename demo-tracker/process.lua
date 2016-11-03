--------------------------------------------------------------------------------
-- FixedCam: complete vision system processing for fixed cameras
-- processing involves:
--    1: saliency / attention to select part of image of interest
--    2: detection of target objects with deep neural networks (eg.: person detection)
--    3: tracking of detected objects [extern lib or on deep net output features]
--    4: face detection [and alignment, if necessary]
--    5: face identification using face-id deep neural networks
--
-- Abhishek,
-- E. Culurciello.
-- August 2015-2016
--------------------------------------------------------------------------------

-- NOTE 1: once an object is recognized as target a few times -->
--         track it, and ignore motion in the same region

--require 'env'

local process = {} -- process object

local ffi = require('ffi')
local vd = assert(require('libvideo_decoder'))
local lib8cc = ffi.load('lib8cc.so')

-- print('==> Loading Face Detector model')
-- URL: http://sourceforge.net/projects/dclib/files/dlib/v18.10/shape_predictor_68_face_landmarks.dat.bz2
if opt.faceid then
   dLib = assert(require('libface_align'))
   dLib.loadmodel('shape_predictor_68_face_landmarks.dat')
end

ffi.cdef [[
   int connectedComponent(int *outputMaskPointer, int *coordinates, int coordinatesSize, int height, int width);
]]

local objects = {} -- data structure to store objects to track (eg.: "person" objects)

local decayLimit = 3     -- # of frames an object will keep on shown even if it is not detected
local minStillFrames = 3 -- # of frames an object has to remain still, until it is actually tagged as still object
local skipFrames = 1 -- number of frames to skip for temporal difference motion finding
local previmg = nil
local prevresult = nil
local tempdiff = nil
local resizer = 8 -- how much to downsample images
local binarizer = 2 -- threshold to binarize image (2: elab video, 4 kitti videos)
local nra = 2 -- running average of # frames
local nca = 2 -- running average for class detections
local itercnt = 0 -- iteration counter counting frames / integrate results
local ytdbuffer = nil
local maxBlobs = 10
local nBlobs = 0
local bminsize = 64--math.max(source.w, source.h)*1/10 -- minimum blob size
local blobData = torch.Tensor(maxBlobs, 4):zero():int() -- coordinates of blob
local enlarger = 1.2 -- blob size extension used for detections
local tracker_started = false
local personCatNum -- person category number in categories list categories.txt
local cam

-- this functions finds all moving blobs:
local function saliency_processor(img)
   -- here saliency is implemented as a temporal difference motion detection
   -- TODO: implement full saliency with deep neural net / saliency algorithm
   local yimg

   if opt.livecam then
      yimg = yimg or torch.ByteTensor(img:size(2)/resizer, img:size(3)/resizer)
      cam = cam or (opt.input == 'geocam' and require 'geolivecam' or require 'livecam')
      cam.frame_y(yimg)
   else
      yimg = image.scale(image.rgb2y(img), img:size(3)/resizer, img:size(2)/resizer)
   end
   previmg = previmg or yimg

   ytdbuffer = ytdbuffer or torch.Tensor(nra, img:size(2)/resizer, img:size(3)/resizer):zero()
   local ytd
   -- compute temporal difference image:
   if yimg:type() == 'torch.ByteTensor' then
      ytd = torch.add(previmg, -yimg)
      previmg = yimg

      -- -- detect moving blobs in fixed cameras:
      -- -- http://sidekick.windforwings.com/2012/12/opencv-motion-detection-based-action_5.html
      -- -- https://blog.cedric.ws/opencv-simple-motion-detection
      -- -- http://www.learnopencv.com/blob-detection-using-opencv-python-c/

      -- print(ytd:max(), ytd:mean()) -- if motion not detect: enable this to debug, adjust "binarizer"
      -- binarizer = math.max(binarizer, ytd:max()*9/10) -- need to set automatically, maybe using vd.diffimages
      binarizer = 20
      ytd:apply(function(x) return (x < binarizer or x > 256-binarizer) and 0 or 1 end)
   else
      ytd = torch.add(previmg, -yimg)
      ytd:abs()
      previmg = yimg

      -- -- detect moving blobs in fixed cameras:
      -- -- http://sidekick.windforwings.com/2012/12/opencv-motion-detection-based-action_5.html
      -- -- https://blog.cedric.ws/opencv-simple-motion-detection
      -- -- http://www.learnopencv.com/blob-detection-using-opencv-python-c/

      -- print(ytd:max(), ytd:mean()) -- if motion not detect: enable this to debug, adjust "binarizer"
      -- binarizer = math.max(binarizer, ytd:max()*9/10) -- need to set automatically, maybe using vd.diffimages
      binarizer = 0.2
      ytd:apply(function(x) return (x < binarizer) and 0 or 1 end)
   end

   -- --running average of binary image:
   ytdbuffer[(itercnt-1)%nra+1] = ytd
   local ytdra = ytdbuffer:sum(1)

   -- dilate to find moving blobs:
   --local ytded  = image.erode(ytdra[1], torch.ones(10,10))
   local ytded  = image.dilate(ytdra[1], torch.ones(9,9))
   ytded  = image.erode(ytded, torch.ones(5,5))

   -- debug:
   -- win2 = image.display{image=ytded, win=win2}

   -- save example if needed:
   if opt.loglevel >=5 then
     if itercnt == 20 then image.save('blob.jpg', ytded) end
   end

   -- pad image for algorithm to work:
   local bp = torch.IntTensor(ytded:size(1) + 2, ytded:size(2) + 2):zero()
   bp[{{2, ytded:size(1) + 1}, {2, ytded:size(2) + 1}}] = ytded
   -- detect blobs and get data
   nBlobs = lib8cc.connectedComponent(torch.data(bp), torch.data(blobData), maxBlobs, bp:size(1), bp:size(2))

   -- print if required:
   if opt.loglevel >=4 then
      for i=1, nBlobs do
         print('Blobs #', nBlobs)
      end
      -- print(blobData)
   end

   if nBlobs > 0 and nBlobs < maxBlobs then
      return blobData[{{1,nBlobs}}]:add(-1)*resizer -- to rescale the data to full sizes coordinates
   else
      return nil
   end
end

local function faceProcess(object)
   if object.target and object.crop then
      local face_scld = object.crop:clone()
      face_scld:add(-torch.min(face_scld))
      face_scld:div(torch.max(face_scld))

      nimages = dLib.facealign(face_scld) -- detect/align faces
      print('Faces found:', nimages)
      if nimages >= 1 then
         object.faceImg, coordinates = dLib.getimage(0) -- get face images
         -- scale for use with neural net:
         local fimg = image.scale(object.faceImg, opt.is, opt.is)
         -- normalize the input:
         for c = 1,3 do
            fimg[c]:add(-network.facestat.mean[c])
            fimg[c]:div( network.facestat.std [c])
         end
         -- identify face with trained face network:
         local faceIdRes = network.facemodel:forward(fimg)
         _, fidx = faceIdRes:sort(true)
         object.faceid = network.faceclasses[fidx[1]] -- save face identity to object
      end
   end
   -- debug:
   -- img1 = img[{{}, {}, {math.max(d.y1, 1), math.min(d.y1+d.dy, img:size(3)) },
                              -- {math.max(d.x1, 1), math.min(d.x1+d.dx, img:size(4)) }}]
   -- win2 = image.display{image=img1, win=win2}
end

--[[
local function trackStart(img,o) -- image, object
   o.trackImg = img[{ {}, {math.max(o.coor.y1, 1), math.min(o.coor.y1+o.coor.dy, img:size(2)) },
                          {math.max(o.coor.x1, 1), math.min(o.coor.x1+o.coor.dx, img:size(3)) }}]:clone()
   dLib.trackstart(img, o.coor.cx, o.coor.cy, o.coor.dx, o.coor.dy)
   print('Tracker started on object:', o.id)
   print('Tracker init coordinates:', o.coor.x1, o.coor.y1, o.coor.dx, o.coor.dy)
   o.tracked = true

   -- debug:
   -- win2 = image.display{image=o.trackImg, win=win2}
   -- io.read()
   -- os.execute('sleep 5')
end


local function trackNext(img,oc) -- image, object
   local nd = {}
   nd.x1, nd.y1, nd.dx, nd.dy = dLib.tracknext(img)
   print('Tracker new  coordinates:', nd.x1, nd.y1, nd.dx, nd.dy, 'object id', oc.id)
   nd.x2 = nd.x1 + nd.dx
   nd.y2 = nd.y1 + nd.dy
   nd.cx = (nd.x1 + nd.x2)/2 -- center x of detection
   nd.cy = (nd.y2 + nd.y1)/2 -- center y of detection
   nd.cs = math.max(nd.dx, nd.dy, 1)/2
   oc.coor = nd -- update object coordinates based on tracker results

   -- debug:
   nextimg = img[{ {}, {math.max(nd.y1, 1), math.min(nd.y1+nd.dy, img:size(2)) },
                       {math.max(nd.x1, 1), math.min(nd.x1+nd.dx, img:size(3)) }}]:clone()
   win3 = image.display{image=nextimg, win=win3}
   -- io.read()
   -- os.execute('sleep 0.1')
end
--]]

local function applynetwork(img, rect)

   local d = {}
   d.x1 = rect.x1
   d.y1 = rect.y1
   d.dx = rect.dx
   d.dy = rect.dy
   -- Expand the rectangle to be a square
   if d.dx < d.dy then
      d.x1 = d.x1 - (d.dy - d.dx) / 2
      d.dx = d.dy
   else
      d.y1 = d.y1 - (d.dx - d.dy) / 2
      d.dy = d.dx
   end

   -- Reduce the square, if it's higher of the frame
   if d.dy > img:size(2) then
      d.x1 = d.x1 + (d.dy - img:size(2)) / 2
      d.y1 = d.y1 + (d.dy - img:size(2)) / 2
      d.dy = img:size(2)
      d.dx = d.dy;
   end

   -- Put the square inside the frame
   if d.x1 + d.dx > img:size(3) then
      d.x1 = img:size(3) - d.dx;
   elseif d.x1 < 0 then
      d.x1 = 0;
   end
   if d.y1 + d.dy > img:size(2) then
      d.y1 = img:size(2) - d.dy;
   elseif d.y1 < 0 then
      d.y1 = 0;
   end

   d.x2 = d.x1 + d.dx
   d.y2 = d.y1 + d.dy

   local crop = img[{ {}, {d.y1+1, d.y2}, {d.x1+1, d.x2} }]
   local scld = image.scale(crop, opt.is, opt.is)
   if scld:type() == 'torch.ByteTensor' then
      scld = scld:float() * (1/255)
   end

   -- normalize the input:
   for c = 1,3 do
      scld[c]:add(-network.stat.mean[c])
      scld[c]:div( network.stat.std [c])
   end

   -- computing network output
   prevresult = network.model:forward(scld)
   return prevresult, scld
end

local function prep_process_cpu(opt, source, network)
   local eye = opt.is

   process.forward = function(img, objectID)

      itercnt = itercnt + 1 -- frame count

      -- skip frames if needed:
      if itercnt > 1 and itercnt%skipFrames ~= 0 then
         return img, objects

      else
         if img:dim() == 3 then
            img = img:view(1,img:size(1),img:size(2),img:size(3))
         end
         -- de-noise the input image:
         dimg = img[1]
         --[[dimg = image.convolve(img[1], image.gaussian(3), 'same')
         for c = 1,3 do
            dimg[c]:add(-network.stat.mean[c])
            dimg[c]:div( network.stat.std [c])
         end
         --]]

         local blobs
         if opt.hwmotion and opt.input == 'geocam' and opt.livecam then
            cam = cam or (opt.input == 'geocam' and require 'geolivecam' or require 'livecam')
            local n = cam.motion_rects(blobData)
            if n > 0 then
               blobs = blobData[{{1,n}}]
            else
               blobs = nil
            end
         else
            blobs = saliency_processor(dimg) -- saliency algorithm
         end
         --print(dp)
         --print(dimg:size())
         if blobs ~= nil then

            -- once we have motion, we find 'objects' which are targets of interest (tracks + data)
            for i=1, blobs:size(1) do
               -- crop a square image based on blob
               local d = {}
               d.dx = blobs[i][3] - blobs[i][1]
               d.dy = blobs[i][4] - blobs[i][2]

               -- check if motion blobs are big enough or too big
               if (d.dx > bminsize) and (d.dy > bminsize) then -- bminsize is smallest size allowed
                  --(d.dx < img:size(4) * 9/10) and (d.dy < img:size(3) * 9/10) then -- 9/10 of img is largest allowed

                  d.x1 = blobs[i][1]
                  d.y1 = blobs[i][2]
                  d.x2 = blobs[i][3]
                  d.y2 = blobs[i][4]
                  prevresult, scld = applynetwork(img[1], d)

                  -- iterate on all found objects to update coordinates:
                  local b2o = 0 -- blobs belongs to object b2o (if 0 belongs to no object()
                  for j, o in ipairs(objects) do
                     --print("cx "..d.cx.." c.cx "..o.coor.cx)
                     --print("cy "..d.cy.." c.cy "..o.coor.cy)
                     --print("dx "..math.max(o.coor.dx, d.dx).." dy "..math.max(o.coor.dy, d.dy))

                     -- check if new blob is part of another object / overlaps
                     -- overlap variable gives the % overlap of existing object with current blob
                     local x1 = math.max(d.x1, o.coor.x1)
                     local x2 = math.min(d.x2, o.coor.x2)
                     local y1 = math.max(d.y1, o.coor.y1)
                     local y2 = math.min(d.y2, o.coor.y2)

                     minObjArea = math.min(d.dx * d.dy, o.coor.dx * o.coor.dy)
                     overlap = (math.abs(y2 - y1) * math.abs(x2 - x1))/minObjArea
                     if overlap > 0.5 and (y2 - y1) > 0 and (x2 - x1) > 0 then
                        if b2o ~= 0 then
                           table.remove(objects,j)
                        else
                           b2o = j
                           -- When there is sudden DECREASE in bounding box area,
                           -- most probably it is about to become still and only
                           -- some parts (hands, head, etc.) are in motion.
                           local newArea = d.dx * d.dy
                           local oldArea = o.coor.dx * o.coor.dy
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
                     new.id = objectID[1] + 1 -- id of object
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
                     objectID[1] = objectID[1] + 1
                     table.insert(objects, new)

                  elseif not target and newObject then -- case 2
                     -- print('case2')
                     local new = {}
                     new.id = objectID[1] + 1
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
                        oc.id = objectID[1] + 1 -- id of object
                        objectID[1] = objectID[1] + 1
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
               prevresult = applynetwork(img[1], o.coor)

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
            if opt.faceid then faceProcess(o) end

            -- tracking start:
            -- if o.target and not o.tracked then
               -- if o.detections > nca then trackStart(img[1], o) end
            -- end

         end

         -- remove, delete, eliminate INACTIVE OBJECTS and candidates:
         for j, o in ipairs(objects) do

            -- if o.detections then o.detections = o.detections-1
            -- if o.detections < 1 then table.remove(objects, j) end
         end
         -- debug:
         -- io.read()
         -- print('Number of active objects:', #objects)

      end
      --objectID = objectID + 1
      return img, objects
   end
end


function process:init(opt, source, network)
   for i=1,#network.classes do
      if network.classes[i] == "person" then personCatNum = i end
   end
   
   print('Person category number is:', personCatNum)
   
   if (opt.spatial == 2) then
      prep_process_cpu(opt, source, network)
   end

end


return process

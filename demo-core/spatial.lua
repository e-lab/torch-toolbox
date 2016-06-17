local ffi = require('ffi')
local lib8cc = ffi.load('lib8cc.so') -- connected component library
ffi.cdef [[
   int connectedComponent(int *outputMaskPointer, int *coordinates, int coordinatesSize, int height, int width);
]]
local spatial = {}
local maxblobs = 10
local blobdata = torch.IntTensor(maxblobs, 4):zero() -- coordinates of blobs

local function makespatial(net, img)

   local newnet = nn.Sequential()
   for i = 1, #net.modules do
      local module = net.modules[i]
      local name = module.__typename
      if name == 'nn.Linear' then
         local newmodule = nn.SpatialConvolutionMM(img:size(1), module.weight:size(1), img:size(2), img:size(3))
         newmodule.weight:copy(module.weight):resize(newmodule.weight:size())
         newmodule.bias:copy(module.bias)
         newnet:add(newmodule)
         img = newmodule:forward(img)
      elseif name == 'nn.Sequential' then
         newnet:add(makespatial(module, img))
         img = module:forward(img)
      elseif name ~= 'nn.Dropout' and
         name ~= 'nn.LogSoftMax' and
         name ~= 'nn.Reshape' and
         name ~= 'nn.View' then
         newnet:add(module)
         img = module:forward(img)
      end
   end
   return newnet

end

local function findblobs(plane)

   local padded = torch.IntTensor(plane:size(1) + 2, plane:size(2) + 2):zero()
   padded[{{2, plane:size(1) + 1}, {2, plane:size(2) + 1}}] = plane
   local nblobs = lib8cc.connectedComponent(torch.data(padded), torch.data(blobdata), maxblobs, padded:size(1), padded:size(2))
   return nblobs > 0 and blobdata[{{1,nblobs}}] or nil

end

-- Take blobs of results over threshold and put them in dsl
local function insertblobs(dsl, results, target, idx, sw, sh, spatial, pdt)

   local blobs = findblobs(results:gt(pdt))
   if blobs then
      for i = 1,blobs:size(1) do
         local d = {}
         d.x1 = blobs[i][1]
         d.y1 = blobs[i][2]
         d.x2 = blobs[i][3]
         d.y2 = blobs[i][4]
         if (d.x2 + d.y2 > 0) and (spatial==1 or spatial==2 and (d.x2-d.x1 >= 2) and (d.y2-d.y1 >= 2)) then
            local entry = {}
            entry.class = target
            entry.value = results:sub(d.y1,d.y2,d.x1,d.x2):max()
            entry.idx = idx
            entry.x1 = (d.x1-1)*sw
            entry.y1 = (d.y1-1)*sh
            entry.dx = (d.x2-d.x1+1)*sw
            entry.dy = (d.y2-d.y1+1)*sh
            table.insert(dsl, entry)
         end
      end
   end

end

local function prep_localize(opt, source)

   spatial.localize = function(results, classes, revClasses)

      local dsl = {}
      -- scaling factors
      local sw = source.w/results:size(3)
      local sh = source.h/results:size(2)

      if opt.targets == nil or opt.targets == '' then
         for k=1,#classes do
            insertblobs(dsl, results[k], classes[k], k, sw, sh, opt.spatial, opt.pdt)
         end
      else
         for k, target in ipairs(opt.targets:split(',')) do
            if revClasses[target] then
               insertblobs(dsl, results[ revClasses[target] ], target, k, sw, sh, opt.spatial, opt.pdt)
            end
         end
      end
      return dsl

   end -- spatial.localize

end

local function modify_for_detection(net)

   -- remove softmax if it exists
   if net.modules[#net.modules].__typename == 'nn.SoftMax' then
      net:remove(#net.modules)
   end
   -- scale the score by 0.05, to be found experimentally
   net:add(nn.MulConstant(0.05, true))
   net:add(nn.Clamp(0, 1))
   return net

end

--[[

   opt fields
      is          eye size
      spatial     spatial mode 0, 1 or 2
      localize    if we neeed localization
      detection   if we need detection instead of classification
      pdt         threshold for localization
      targets     targets to localize

   source fields
      w           image width
      h           image height

--]]
function spatial:init(opt, net, source)

   if opt.spatial > 0 then
      local sampleimg = torch.Tensor(3, opt.is, opt.is)
      net = makespatial(net, sampleimg)
      if opt.localize then
         prep_localize(opt, source)
      end
   end
   if opt.detection or opt.localize then
      net = modify_for_detection(net)
   end
   self.net = net
   return net

end

return spatial

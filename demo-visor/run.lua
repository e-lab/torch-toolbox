--------------------------------------------------------------------------------
-- Generic detector demo
-- E-Lab 2016
--------------------------------------------------------------------------------

-- Requires --------------------------------------------------------------------
require 'pl'
require 'nn'
require 'sys'
require 'paths'
require 'image'
package.path = '../demo-core/?.lua;' .. package.path
local display = assert(require('display'))
local process = assert(require('process'))
vid = assert(require('libvideo_decoder'))

-- Local definitions -----------------------------------------------------------
local pf = function(...) print(string.format(...)) end
local Cr = sys.COLORS.red
local Cb = sys.COLORS.blue
local Cg = sys.COLORS.green
local Cn = sys.COLORS.none

-- Title definition -----------------------------------------------------------
title = [[
 **** e-lab visor demo ****
 ]]

-- Options ---------------------------------------------------------------------
local opt = lapp(title .. [[
-m, --model      (default 'generic.model')    File or directory containing the model
-p, --platform   (default cpu)                Select profiling platform (cpu|cuda|nnx)
--nt             (default 8)                  Number of threads for multiprocessing
--is             (default 231)                Image's side length
--camRes         (default QHD)                Camera resolution (QHD|VGA|FWVGA|HD|FHD)
-i, --input      (default cam0)               Input file name or camera as cam0, cam1, ...
-z, --zoom       (default 1)                  Zoom ouput window
--batch          (default 1)                  Batch size of images for batch processing
--pdt            (default 0.5)                Detection threshold to detect person vs background
--fps            (default 30)                 Frames per second (camera setting)
--gui                                         Use GUI display (default false) which sends to server
--dv             (default 0)                  Verbosity of detection: 0=1target, >0:#top categories to show
--diw            (default 1)                  number of frames for integration of object detection
--spatial        (default 0)                  Spatial mode: 0) resize and take central square of size opt.is, 1) resize to make height=opt.is, 2) process entire image as is
-l, --localize                                Localization of target with bounding boxes
--loglevel       (default 1)                  Logging level from 0 (no logging) to 10
--noconsole                                   Don't display logging info to console
--maxloops       (default 0)                  Only run N loops (0=unlimited)
--diffsens       (default 0)                  If different from 0, only process images with > diffsens difference in pixels (relative to max)
--diffarea       (default 0.001)              If diffsens > 0, ratio of pixels that need to be over diffsens
--encodeto       (default no)                 Encode the input to the given file
--targets        (default '')                 Only consider the given comma-separated categories
--detection                                   Turns on <detection> mode (independent logistic renormalisation)
--saveembedding  (default 0)                  Save network output for each image in text (1) or binary, t7 (2) format
--pyramid                                     Process the images rescaled to several resolutions starting at camRes
--map            (default -1)                 Show detected zones with higher luminance (-1: disabled, 0:on pyramid max, >0: on that resolution index)
--verbose        (default true)               Print percentage value and names on left corner
]])

pf(Cg..title..Cn)
torch.setdefaulttensortype('torch.FloatTensor')
torch.setnumthreads(opt.nt)
print('Number of threads used:', torch.getnumthreads())
vid.loglevel(opt.loglevel)
local frame
local isimage
if string.sub(opt.input, 1, 3)  == 'cam' and tonumber(string.sub(opt.input,4,-1)) ~= nil then
   if opt.pyramid then
      error('pyramid mode is not supported for camera input')
   end
   frame = assert(require('framecamera'))
elseif opt.input:lower():match('%.jpe?g$') or opt.input:lower():match('%.png$') then
   frame = assert(require('frameimage'))
   isimage = 1
elseif paths.dirp(opt.input) then
   frame = assert(require('frameimage'))
   isimage = 2
else
   frame = assert(require('framevideo'))
end

-- checks and balances:
if opt.spatial > 2 or opt.spatial < 0 then
   error('--spatial have wrong value range')
end

if opt.pyramid and opt.spatial ~= 2 then
   error('pyramid only works in spatial mode 2')
end

-- global objects:
local network = {} -- network object
local source = {} -- source object

-- load categories.txt file
local function readCatCSV(filepath)
   network.labels = {}
   local lineno = 1
   local file = io.open(filepath,'r')
   file:read() -- throw away first line of category file
   local fline = file:read()
   while fline ~= nil do
      local col1 = fline:match("([^,]+),([^,]+)")
      network.labels[lineno] = col1
      lineno = lineno + 1
      fline = file:read()
   end
   file:close()
end
local function readClasses(filePath)
   network.labels = {}
   local lineno = 1
   local file = torch.load(filePath)
   print(file)
   for i = 1, #file do
      network.labels[lineno] = file[lineno]
      lineno = lineno + 1
   end
end
local function readAux(filePath)
   network.labels = {}
   local lineno = 1
   local file = torch.load(filePath)
   print(file.classes[1])
   for i = 1, #file.classes do
      network.labels[lineno] = file.classes[lineno]
      lineno = lineno + 1
   end
end
-- Loading neural network:
if paths.filep(opt.model) then
   network = torch.load(opt.model)
elseif paths.filep(opt.model .. '/model.net') then
   model = torch.load(opt.model .. '/model.net')
   if model.modules[#model].__typename == 'nn.LogSoftMax' then
      model:remove(#model)
      model:add(nn.SoftMax():float())
   end
   network.net = model
   print(opt.model .. '/aux.t7')
   if paths.filep(opt.model .. '/categories.txt') then
      readCatCSV(opt.model .. '/categories.txt')
   elseif paths.filep(opt.model .. '/classes.t7') then
      readClasses(opt.model .. '/classes.t7')
   elseif paths.filep(opt.model .. '/aux.t7') then
      readAux(opt.model .. '/aux.t7')
   else
      error('no categories.txt file found in directory: ' .. opt.model)
   end
   if paths.filep(opt.model .. '/stat.t7') then
      local stat = torch.load(opt.model .. '/stat.t7')
      network.mean = stat.mean
      network.std = stat.std
   end
else
   error('Cannot find any model in ' .. opt.model)
end

if #network.labels == 0 then
   error('categories.txt contains no categories')
end

-- switch input sources
source.res = {
   HVGA  = {w =  320, h =  240},
   QHD   = {w =  640, h =  360},
   VGA   = {w =  640, h =  480},
   FWVGA = {w =  854, h =  480},
   HD    = {w = 1280, h =  720},
   FHD   = {w = 1920, h = 1080},
}
source.w = source.res[opt.camRes].w
source.h = source.res[opt.camRes].h
source.fps = opt.fps
local src = torch.FloatTensor(3, source.h, source.w)


-- init application packages
frame:init(opt, source)
display:init(opt, source, network.labels)
process:init(opt, source, network)

local downs = 1
for i=1, #network.net.modules do
   if network.net.modules[i].dW ~= nil then
      downs = downs * network.net.modules[i].dW
   end
end

if opt.map >= 0 then
   for idx,val in ipairs(network.labels) do
      if val == opt.targets then
         mapidx = idx
      end
   end
   if not mapidx then
      error('--map requires one unique existing target')
   end
   if opt.pyramid then
      print("The image is processed at these resolutions: ")
      for i = 1,#source.resolutions do
         print(source.resolutions[i][1] .. 'x' .. source.resolutions[i][2])
      end
   end
end

-- profiling timers
local timer = torch.Timer()
local t_loop = 1 -- init to 1s


if opt.encodeto ~= 'no' then
   vid.encoderopen(nil, opt.encodeto, source.w, source.h, source.fps)
end

local function saveembedding(result, filename)
   ext = opt.saveembedding == 1 and ".txt" or ".t7"
   if isimage == 1 then
      outfile = opt.input .. ext
   elseif string.sub(opt.input,-1,-1) == '/' then
      outfile = opt.input .. filename .. ext
   else
      outfile = opt.input .. '/' .. filename .. ext
   end
   torch.save(outfile, result, opt.saveembedding == 1 and 'ascii' or 'binary')
end

n = 0
motion = opt.diffsens > 0 and opt.spatial == 0
while display.continue() do
   timer:reset()

   local result = {}
   local img
   nres = source.resolutions and #source.resolutions or 1
   local term = false
   for res_idx = 1,nres do
      if motion then
         src = process.motionframe(src, frame)
      else
         src, filenames = frame.forward(src, res_idx)
      end
      if not src then
         term = true
         break
      end
      if opt.encodeto ~= 'no' and res_idx == 1 then
         if src:dim() == 3 then
            vid.encoderwrite(src)
         else
            for i=1,src:size(1) do
               vid.encoderwrite(src[i])
            end
         end
      end

      if res_idx == 1 then
         result[1], img = process.forward(src)
      else
         result[res_idx] = process.forward(src)
      end
      if nres > 1 then
         result[res_idx] = result[res_idx]:clone()
      end
   end
   if term then
      break
   end

   for i = 1,img:size(1) do
      if opt.pyramid then
         local res = vid.expandconvresult(result[1][i], opt.is / downs - 1)
         local w = res:size(3) * 2
         local h = res:size(2) * 2
         res = image.scale(res, w, h)
         if opt.map == 1 then
            local t = image.scale(res[mapidx], img:size(4), img:size(3), 'simple')
            img[i][1] = img[i][1]:cmul(t)
            img[i][2] = img[i][2]:cmul(t)
            img[i][3] = img[i][3]:cmul(t)
         end
         for j = 2,nres do
            local res2 = vid.expandconvresult(result[j][i], opt.is / downs - 1)
            if opt.map == j then
               local t = image.scale(res2[mapidx], img:size(4), img:size(3), 'simple')
               img[i][1] = img[i][1]:cmul(t)
               img[i][2] = img[i][2]:cmul(t)
               img[i][3] = img[i][3]:cmul(t)
            end
            res2 = image.scale(res2, w, h)
            res = res:cmax(res2)
         end
         if opt.map == 0 then
            local t = image.scale(res[mapidx], img:size(4), img:size(3), 'simple')
            img[i][1] = img[i][1]:cmul(t)
            img[i][2] = img[i][2]:cmul(t)
            img[i][3] = img[i][3]:cmul(t)
         end
         display.forward(res, img[i], (img:size(1)/t_loop))
      else
         local res
         if opt.localize or opt.map >= 0 then
            -- Never use localize without first doing expandconvresult, it will get all the coordinates wrong, check with two-people.jpg
            res = vid.expandconvresult(result[1][i], opt.is / downs - 1)
         else
            res = result[1][i]
         end
         if opt.map >= 0 then
            local t = image.scale(res[mapidx], img:size(4), img:size(3), 'simple')
            img[i][1] = img[i][1]:cmul(t)
            img[i][2] = img[i][2]:cmul(t)
            img[i][3] = img[i][3]:cmul(t)
         end
         display.forward(res, img[i], (img:size(1)/t_loop))
      end
      if opt.saveembedding > 0 and isimage then
         saveembedding(result[1][i], opt.batch == 1 and filenames.filename or filenames[i].filename)
      end
      if opt.gui and isimage then
         io.read()
      end
   end

   t_loop = timer:time().real

   collectgarbage()
   n = n+1
   if n == opt.maxloops then
      break
   end
end
print('process done!')

if opt.encodeto ~= 'no' then
   vid.encoderclose()
end
display.close()

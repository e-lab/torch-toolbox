--------------------------------------------------------------------------------
-- tracker: complete object tracking system processing for fixed cameras
--
-- Abhishek, E. Culurciello, August 2015-2016
-- 
--------------------------------------------------------------------------------

-- Requires --------------------------------------------------------------------
require 'pl'
require 'nn'
require 'sys'
require 'paths'
require 'image'

-- Local definitions -----------------------------------------------------------
local pf = function(...) print(string.format(...)) end
local Cr = sys.COLORS.red
local Cb = sys.COLORS.blue
local Cg = sys.COLORS.green
local Cn = sys.COLORS.none
local THIS = sys.COLORS.blue .. 'THIS' .. Cn

-- Title definition -----------------------------------------------------------
title = [[
          _         _    
  ___ ___| |   __ _| |__ 
 / -_)___| |__/ _` | '_ \
 \___|   |____\__,_|_.__/
                         
]]

-- Options ---------------------------------------------------------------------
opt = lapp(title .. [[
--mode           (default 'pc')               Use mode: pc, device, server
-d, --dmodel     (default '../../models/')    Base Directory models/ where models are stored
-m, --model      (default 'home-basic')       Directory for model.net and categories.txt files
-p, --platform   (default cpu)                Select profiling platform (cpu|cuda|nnx)
--nt             (default 8)                  Number of threads for multiprocessing
--is             (default 231)                Image's side length
--camRes         (default QHD)                Camera resolution (QHD|VGA|FWVGA|HD|FHD)
--fresize                                     if true we force to resize the video input to camRes
-i, --input      (default cam0)               Input file name or camera as cam0, cam1, ...
-c, --cam        (default 0)                  Camera device number
-z, --zoom       (default 1)                  Zoom ouput window
--batch          (default 1)                  Batch size of images for batch processing
--camname        (default cam_0)              Name of IP camera provided by user
--pdt            (default 0.5)                Detection threshold to detect person vs background
--fps            (default 30)                 Frames per second (camera setting)
--gui            (default true)               Use GUI display (default false) which sends to server
--dv             (default 0)                  Verbosity of detection: 0=1target, >0:#top categories to show
--diw            (default 6)                  number of frames for integration of object detection
--wvtt                                        Write caption file VTT format
--wjson                                       Write caption file JSON format
--wthumbs        (default 0)                  Write nothing (0) / thumbnails (1) / videos (2) of detections to thumbs folder
--wvideolen      (default 10)                 Length of saved video around detections (in seconds for both before and after detection)
--wdt                                         Write detection cropped image for use in creating datasets
--spatial        (default 2)                  Spatial mode: 0) none, 1) height=source.h, 2) height = opt.is (0=no spatial mode, 1=large size, 2=small size)
-l, --localize   (default true)               Localization of target with bounding boxes
--allcat                                      Use all categories in categories file!
--loglevel       (default 1)                  Logging level from 0 (no logging) to 10
--console        (default true)               Print display logging info to console
--release                                     Set this script to be ready for deployment
--diffsens       (default 0.02)               If different from 0, only process images with > diffsens difference in pixels (relative to max)
--diffarea       (default 0.05)               If diffsens > 0, ratio of pixels that need to be over diffsens
--maxloops       (default 0)                  Only run N loops (0=unlimited)
--faceid                                      Run face identification code also
--livecam                                     Use the livecam library for faster preview
--fullscreen                                  Full screen (requires livecam)
--hwmotion                                    Use HW motion detection of geocam
]])

local frame = assert(require('frame'))
local display = assert(require('display'))
local process = assert(require('process'))
vid = assert(require('libvideo_decoder'))

pf(Cb..title..Cn)
torch.setdefaulttensortype('torch.FloatTensor')
torch.setnumthreads(opt.nt)
print('Number of threads used:', torch.getnumthreads())

-- checks and balances:
if opt.spatial>2 or opt.spatial<0 or opt.wthumbs>2 or opt.wthumbs<0 then
   pf(Cr .. 'Error: opt.spatial>2' .. Cn)
   os.exit()
end

-- to load a 64-bit model in binary, we override torch.DiskFile if 32bit machine (ARM):
local systembit = tonumber(io.popen("getconf LONG_BIT"):read('*a'))
if systembit == 32 then
   require('libbincompat')
end

-- global objects:
network = {} -- network object
source = {} -- source object

-- Loading neural network:
local model_file = opt.dmodel .. opt.model .. '/'.. 'model.net'
if paths.filep(model_file) then
   if opt.release then
      network.model = require('loadmodel')(model_file)
   else
      network.model = torch.load(model_file)
   end
elseif paths.filep(model_file..'.ascii') then
   if opt.release then
      network.model = require('loadmodel')(model_file..'.ascii', 'ascii')
   else
      network.model = torch.load(model_file..'.ascii', 'ascii')
   end
else
   error('no model file found in directory: '..opt.dmodel..opt.model)
end

-- load face identification neural network
if opt.faceid then
   opt.facemodel = 'td-elab'
   network.facemodel = torch.load(opt.dmodel .. opt.facemodel .. '/'.. 'model.net')
   network.facestat  = torch.load(opt.dmodel .. opt.facemodel .. '/'.. 'stat.t7')
end

-- Loading classes names and also statistics from dataset
local stat_file = opt.dmodel .. opt.model .. '/'.. 'stat.t7'
if paths.filep(stat_file) then
   network.stat = torch.load(stat_file)
elseif paths.filep(stat_file..'.ascii') then
   network.stat = torch.load(stat_file..'.ascii', 'ascii')
else
   error('no stat file found in directory: '..opt.dmodel..opt.model)
end

-- change targets based on categories csv file:
function readCatCSV(filepath)
   local file = io.open(filepath,'r')
   local classes = {}
   local targets = {}
   file:read() -- throw away first line of category file
   local fline = file:read()
   while fline ~= nil do
      local col1, col2 = fline:match("([^,]+),([^,]+)")
      table.insert(classes, col1)
      table.insert(targets, ('1' == col2))
      fline = file:read()
   end
   return classes, targets
end

-- load categories from file folder or this (run.lua) folder
local newcatdir = opt.dmodel .. opt.model .. '/'.. 'categories.txt'
pf(Cg .. 'Local mode: loading categories file from: ' .. newcatdir .. Cn)
network.classes, network.targets = readCatCSV(newcatdir)
-- read face categories:
if opt.faceid then
   network.faceclasses = readCatCSV(opt.dmodel .. opt.facemodel .. '/'.. 'categories.txt')
end

if #network.classes == 0 then
   error('Categories file contains no categories')
end

pf(Cg..'Network has this list of categories, targets:'..Cn)
for i=1,#network.classes do
   if opt.allcat then network.targets[i] = true end
   pf(Cb..i..'\t'..Cn..network.classes[i]..Cr..'\t'..tostring(network.targets[i])..Cn)
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
display:init(opt, source, network.classes, network.targets)
process:init(opt, source, network)

-- profiling timers
local timer = torch.Timer()
local t_loop = 1 -- init to 1s

local objectID = torch.DoubleTensor(1,1):zero()

-- create main functions
local main = function()
   local n = 0
   while display.continue() do
      timer:reset()

      src = frame.forward(src)
      if not src then
         break
      end

      local img, objects = process.forward(src, objectID)

      if img:dim() == 3 then
         display.forward(img, objects, (1/t_loop))
      else
         for i = 1,img:size(1) do
            display.forward(img[i], objects, (img:size(1)/t_loop))
         end
      end

      t_loop = timer:time().real

      if n%10 == 0 then collectgarbage() end
      n = n+1
   end
   print('process done!')
   display.close()
end

-- execute main loop
main()

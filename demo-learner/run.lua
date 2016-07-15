--------------------------------------------------------------------------------
-- Online learning demo loosely based on Y. LeCun demo
-- E. Culurciello, October 2014 - Novemeber 2015
-- with help from A. Canziani, JH Jin, A. Dundar 
--------------------------------------------------------------------------------

-- Requires --------------------------------------------------------------------
require 'pl'
require 'nn'
require 'camera'
require 'sys'
-- display
require 'qt'
require 'qtwidget'
require 'qtuiloader'

-- Title definition -----------------------------------------------------------
local title = [[
          _         _    
  ___ ___| |   __ _| |__ 
 / -_)___| |__/ _` | '_ \
 \___|   |____\__,_|_.__/
                         
]]

-- Options ---------------------------------------------------------------------
opt = lapp(title .. [[
-d, --dmodel     (default '../../models/')   Base Directory models/ where models are stored
-m, --model      (default 'home-basic')      Directory for model.net and categories.txt files
--is             (default 224)               Neural network eye size
--camRes         (default QHD)               Camera resolution ([VGA]|FWVGA|HD|FHD)
--camIdx         (default 0)                 Camera id (0,1,2..)
--nthreads       (default 4)                 Set number of threads for multiprocessing
--file           (default 'session.t7')      Name of session save/load file
--verbose                                    Verbosity: print status information
--largegui                                   Large demo with mouse clicks for prototypes
]])

io.write(title)
torch.setdefaulttensortype('torch.FloatTensor')
torch.setnumthreads(opt.nthreads)

-- to load a 64-bit model in binary, we override torch.DiskFile if 32bit machine (ARM):
local systembit = tonumber(io.popen("getconf LONG_BIT"):read('*a'))
if systembit == 32 then
   require('libbincompat')
end

-- Loading input
local resolutions = {
   QHD   = {w =  640, h =  360}, -- good for 720p cameras
   VGA   = {w =  640, h =  480},
   FWVGA = {w =  854, h =  480},
   HD    = {w = 1280, h =  720},
   FHD   = {w = 1920, h = 1080},
}

-- global objects:
network = {}

-- Loading neural network:
local model_file = opt.dmodel .. opt.model .. '/'.. 'model.net'
if paths.filep(model_file) then
      network.model = torch.load(model_file)
else
   error('no model file found in directory: '..opt.dmodel..opt.model)
end

-- remove last layer (we will not need for learner)
network.model.modules[#network.model.modules] = nil -- softmax
network.model.modules[#network.model.modules-1] = nil -- last layer

-- Loading classes names and also statistics from dataset
local stat_file = opt.dmodel .. opt.model .. '/'.. 'stat.t7'
if paths.filep(stat_file) then
   network.stat = torch.load(stat_file)
else
   error('no stat file found in directory: '..opt.dmodel..opt.model)
end

if opt.verbose then print(net) end

-- start camera:
local mycam = image.Camera{idx = opt.camIdx, width = resolutions[opt.camRes].w, height = resolutions[opt.camRes].h}

-- Displaying routine
local wsos = 2 -- window size offset for displaying
eye = resolutions[opt.camRes].h - wsos
x1  = resolutions[opt.camRes].w / 2 - eye / 2
y1  = resolutions[opt.camRes].h / 2 - eye / 2
x2  = resolutions[opt.camRes].w / 2 + eye / 2
y2  = resolutions[opt.camRes].h / 2 + eye / 2

-- profiling timers
local timer = torch.Timer()

-- local variables:
local maxprotos = 5 -- for now 5 object max
local input, output, detections, frame
local protos = {} -- list or prototypes
local protoIcons = torch.zeros(maxprotos, 3, 32, 32) -- 5 icons: 3x32x32 image size
local bbox = {}


function getFrame(ui)
   frame = mycam:forward()
   if opt.largegui then -- just take the image sized is at mouse location
      frame_normalized = frame:clone()
      for c = 1,3 do
         frame_normalized[c]:add(-network.stat.mean[c])
         frame_normalized[c]:div( network.stat.std [c])
      end
      ui.mouse = ui.mouse or {x=opt.is/2,y=opt.is/2}
      ui.learngui = ui.learngui or {x=opt.is/2,y=opt.is/2}
      local x = ui.mouse.x
      local y = ui.mouse.y
      x = math.max(x, opt.is/2+1)
      x = math.min(x, frame_normalized:size(3) - opt.is/2)
      y = math.max(y, opt.is/2+1)
      y = math.min(y, frame_normalized:size(2) - opt.is/2)
      patch = image.crop(frame_normalized, x-opt.is/2, y-opt.is/2,
                                           x+opt.is/2, y+opt.is/2)
   else
      frame = image.crop(frame, x1, y1, x2, y2)
      patch = image.scale(frame,'^' .. opt.is )
      for c = 1,3 do
         patch[c]:add(-network.stat.mean[c])
         patch[c]:div( network.stat.std [c])
      end
   end
end


function process(ui)

   -- Computing prediction
   if opt.largegui then
      output = network.model:forward(frame_normalized)
   else
      output = network.model:forward(patch)
   end

   -- allocate storage
   local dist_threshold = widget.verticalSlider.value
   if opt.largegui then
      detections = torch.zeros(output:size(2), output:size(3))
   else
      detections = torch.zeros(1,1)
   end
   min_dist = torch.Tensor(detections:size()):fill(dist_threshold)

   -- for each category
   for i = 1, #protos do
      local class_min_dist = dist_threshold

      -- for each prototype
      for j = 1, #protos[i] do
         -- calculate L2 distance
         local prt = protos[i][j]:expandAs(output)
         local dist_L2 = torch.pow(output - prt, 2):sum(1):sqrt()
         -- local dist_L22 = torch.dist(output, prt)
         -- print(dist_L2, dist_L22)
         local distance
         if opt.largegui then
            distance = dist_L2:resize(output:size(2), output:size(3))
         else
            distance = dist_L2:resize(1,1)
         end

         -- update distance if closer than before
         local dist_update_idx = distance:lt(min_dist)
         min_dist[dist_update_idx] = distance[dist_update_idx]
         detections[dist_update_idx] = i

         -- find x,y that has the min dist
         local col, col_i = torch.min(distance, 2)
         local row, row_i = torch.min(col, 1)
         row_i = row_i:squeeze()
         col_i = col_i[row_i][1]

         -- detect obj when ( < dist < threshold)
         if distance[row_i][col_i] < math.min(class_min_dist, dist_threshold) then
            class_min_dist = distance[row_i][col_i]
            bbox[i] = {
               x = col_i,
               y = row_i,
            }
         end
      end
      ui.progBars[i].value = class_min_dist
   end
   -- threshold if it is too far
   detections[min_dist:gt(dist_threshold)] = 0


   -- learn new prototype:
   if (ui.learn and not opt.largegui) or ui.learnmouse then
      if opt.verbose then
         print('Key pressed', ui.currentId)
      end

      if not protos[ui.currentId] then
         protos[ui.currentId] = {}
      end

      --image.display{image=patch, zoom = 4}
      table.insert(protos[ui.currentId], network.model:forward(patch):clone())
      protoIcons[ui.currentId] = image.scale(patch,32,32)
      ui.objLabels[ui.currentId].text = #protos[ui.currentId]
      print('Added Prototype for Object #', ui.currentId)

      if opt.largegui then
         ui.learnmouse = false
      else
         ui.learn = false
      end
   end

end


function controls(ui)
   -- clear memory / save / load session
   if ui.forget then
      ui.logit('clearing memory')
      protos = {} -- reset prototypes
      protoIcons = torch.zeros(5, 3, 32, 32) -- reset icons
      ui.resetProgBars()
      ui.resetObjLabels()
      collectgarbage()
      ui.forget = false
   end
   if ui.save then
      local filen =  opt.file
      ui.logit('saving memory to ' .. filen)
      local file = torch.DiskFile(filen,'w')
      file:writeObject(protos)
      file:close()
      ui.save = false
   end
   if ui.load then
      local filen = opt.file
      ui.logit('reloading memory from ' .. filen)
      local file = torch.DiskFile(filen)
      local loaded = file:readObject()
      proto = loaded
      file:close()
      ui.load = false
   end
end


-- display functions:
if opt.largegui then
   widget = qtuiloader.load('glarge.ui') 
else
  widget = qtuiloader.load('g.ui')
end
painter = qt.QtLuaPainter(widget.frame)


function display(ui)
   painter:gbegin()
   painter:showpage()

   -- display input image
   if opt.largegui then
      image.display{image=frame,win=painter}

      -- draw a circle around mouse
      if ui.mouse then
         local color = ui.colors[ui.currentId]
         local legend = 'learning object #' .. ui.currentId
         local x = ui.mouse.x
         local y = ui.mouse.y
         local w = opt.is
         local h = opt.is
         painter:setcolor(color)
         painter:setlinewidth(3)
         painter:arc(x, y, h/2, 0, 360)
         painter:stroke()
         painter:setfont(qt.QFont{serif=false,italic=false,size=14})
         painter:moveto((x-opt.is/2), (y-opt.is/2-2))
         painter:show(legend)

         for i,box in pairs(bbox) do
            local color = ui.colors[i]
            local legend = 'obj'..i
            local x = (box.x-1)*frame:size(3)/detections:size(2)-opt.is/2 + 1
            local y = (box.y-1)*frame:size(2)/detections:size(1)-opt.is/2 + 1
            painter:setcolor(color)
            painter:setlinewidth(3)
            painter:rectangle(x, y, opt.is, opt.is)
            painter:stroke()
            painter:setfont(qt.QFont{serif=false,italic=false,size=14})
            painter:moveto(x, (y-2))
            painter:show(legend)
         end
         bbox = {}
      end

   else
      image.display{image=frame,win=painter}
   end

   -- draw icons and red box that highests most probable object
   for i = 1, maxprotos do
      local xrec = (opt.largegui and 350+360) or 360
      local yrec = 43*i-25

      -- check if detection exist in small input
      if not opt.largegui and (i == detections[1][1]) then
         painter:rectangle(xrec,yrec,36,36) -- 2pixel bigger than icons
         painter:setcolor('red')
         painter:fill()
      end
      -- paint icons:
      image.display{image=protoIcons[i],win=painter,x=xrec+2, y=yrec+2}
   end

   painter:gend()
end


-- user interface file:
ui = require 'ui'

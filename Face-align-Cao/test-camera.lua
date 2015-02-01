#!/usr/bin/env qlua
--
-- Testing script for FaceAlignment Torch7 binding
--
-- Author: Jonghoon Jin
--
require('image')
require('camera')
local FaceAlign = assert(require('libface_align'))
torch.setdefaulttensortype('torch.FloatTensor')

-- init regressor model parameters
-- URL: https://docs.google.com/uc?export=download&confirm=G1sQ&id=0B0tUTCaZBkccOGZTcjJNcDMwa28
FaceAlign.load('model.txt')

-- prepare input
local cam = image.Camera{}
local img = cam:forward()

-- set bounding box for face
local bb = {
   x = 1,             -- start x
   y = 1,             -- start y
   w = img:size(3),   -- width of bb
   h = img:size(2),   -- height of bb
}

local win_raw, win_aligned
while true do
   img = cam:forward()
   win_raw = image.display{image=img, win=win_raw}
   img_aligned = FaceAlign.process(img, bb.x, bb.y, bb.w, bb.h)
   win_aligned = image.display{image=img_aligned, win=win_aligned}
end

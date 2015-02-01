#!/usr/bin/env qlua
--
-- Testing script for FaceAlignment Torch7 binding
--
-- Author: Jonghoon Jin
--
require('image')
local FaceAlign = assert(require('libface_align'))
torch.setdefaulttensortype('torch.FloatTensor')

-- init regressor model parameters
-- URL: https://docs.google.com/uc?export=download&confirm=G1sQ&id=0B0tUTCaZBkccOGZTcjJNcDMwa28
FaceAlign.load('model.txt')

-- load test image
local filename = 'sample.jpg'
local img = image.load(filename)

-- set bounding box for face
local bb = {
   x = 127,   -- start x
   y = 12,    -- start y
   w = 111,   -- width of bb
   h = 135,   -- height of bb
}

-- align and crop image
local timer = torch.Timer()
local img_aligned = FaceAlign.process(img, bb.x, bb.y, bb.w, bb.h)
print('==> process time [ms]:', timer:time().real*1000)

image.display(img)
image.display(img_aligned)

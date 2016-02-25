#!/usr/bin/env qlua
--
-- Testing script for FaceAlignment Torch7 binding
--
-- Author: Marko Vitez, Jonghoon Jin
--
require('image')
local FaceAlign = assert(require('libface_align'))
torch.setdefaulttensortype('torch.FloatTensor')


print('==> Loading model')
-- URL: http://sourceforge.net/projects/dclib/files/dlib/v18.10/shape_predictor_68_face_landmarks.dat.bz2
FaceAlign.loadmodel('shape_predictor_68_face_landmarks.dat')


-- load image
local img = image.load(arg[1])


-- process image
nimages = FaceAlign.facealign(img)
print('nimages='..nimages)


-- display result
for i=0,nimages-1 do
   local img1 = torch.Tensor()
	local coordinates = FaceAlign.getimage(img1, i)
	image.display(img)
	image.display(img1)
end

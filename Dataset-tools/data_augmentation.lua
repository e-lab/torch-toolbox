-- Author: Aysegul Dundar
-- Date: December, 2014
require 'image'
require 'pl'
require 'trepl'
require 'augment'
require 'paths'

opt = lapp[[
   -h, --hflip              (default true)    horizontal flip
   -t, --transImgInt        (default 20)      stepping of translation, number of images to create (ta/t)^2
   --tl                     (default 40)      max translation length
   -r, --rotationImg        (default 2)       number of images to create by rotation
   --ra                     (default 0.1)     max angle of rotation
   -p, --pathToFolder       (default 'toy')   path to the folder of images
   -e, --extention          (default '.png')  extention
   -g, --gray               (default true)    gray option
   --red                    (default 1)       red value to modify
   --blue                   (default 0)       blue value to modify
   --green                  (default 0)       green value to modify
   --sat                    (default 0.2)     saturation value to apply
   --con                    (default 1)       contrast value to apply
]]

print(opt)
if not paths.dir(opt.pathToFolder) then
  error(string.format("the folder %s not exist", opt.pathToFolder))
end
local ext = opt.extention


local image_names = paths.dir(opt.pathToFolder, 'r')
os.execute("mkdir -p " .. opt.pathToFolder .. "/augmentations")
local aug_path = opt.pathToFolder .. "/augmentations"

for i=1, #image_names do
   local tmp_name = image_names[i]
   if (string.sub(tmp_name, 1, 1) ~= '.' and tmp_name ~= 'augmentations') then
      local src = paths.concat(opt.pathToFolder,tmp_name)
      local dst = paths.concat(aug_path,tmp_name)
      local tmp_img = image.load(src)

      --Translation and flip example
      if opt.tl > 0 then
         crop5_img(opt.hflip, dst, tmp_img, opt.tl, opt.transImgInt, ext)
      elseif opt.hflip == true then
         hflip_img(dst, tmp_img, ext)
      end

      --Rotation example
      if opt.ra > 0 then
         local angle = opt.ra/opt.rotationImg
         for i=1, opt.rotationImg do
             rotate_img(opt.hflip, dst, angle*i, tmp_img, ext)
          end
      end

      --Color jitterion example
      if opt.gray then
         grayscale(dst, tmp_img, ext)
      end

      --Color effect will applied in orders
      ColorJitter(opt.red,opt.green,opt.blue,dst, tmp_img, ext)
      Contrast(opt.con, dst, tmp_img, ext)
      Saturation(opt.sat, dst, tmp_img, ext)
   end
end



-- Author: Aysegul Dundar
-- Date: December, 2014
require 'image'

function imagesFolder(pathToFolder)


   local jitter  = 50
   local image_names = paths.dir(pathToFolder, 'r')
   os.execute("mkdir -p " .. pathToFolder .. "/augmentations")
   local aug_path = pathToFolder .. "/augmentations"

   for i=1, #image_names do

    local tmp_img = image_names[i]
    if (string.sub(tmp_img, 1, 1) ~= '.' and tmp_img ~= 'augmentations') then
       hflip(pathToFolder, tmp_img, aug_path)
       crop4(pathToFolder, tmp_img, aug_path, jitter)
    end
   end


 end


function hflip(path_img, tmp_img, aug_path)

  local tmp_path = path_img .. "/" .. tmp_img
  print(tmp_path)
  local img = image.load(tmp_path)
  local img_flip = image.hflip(img)

  -- assumes .jpg ending for now
  tmp_img = string.sub(tmp_img, 1, -5)
  local img_path = aug_path .. "/" .. tmp_img .. "flip.jpg"
  image.save(img_path, img_flip)

end


function crop4(path_img, tmp_img, aug_path, jitter)

  local tmp_path = path_img .. "/" .. tmp_img
  local img = image.load(tmp_path)

  local w = img:size(2) - jitter
  local h = img:size(3) - jitter

  -- assumes .jpg ending for now
  tmp_img = string.sub(tmp_img, 1, -5)

  local sample = img[{{}, {1, w}, {1, h}}]
  local img_path = aug_path .. "/" .. tmp_img .. "crop1.jpg"
  image.save(img_path, sample)

  for i=1, 2 do
    for j=1, 2 do
       local sample = img[{{}, {1+ (j-1) * jitter, (j-1) * jitter + w}, {1+ (i-1) * jitter, (i-1) * jitter + h}}]
       local img_path = aug_path .. "/" .. tmp_img .. "crop" .. j+i .. ".jpg"
       image.save(img_path, sample)
     end
   end

end



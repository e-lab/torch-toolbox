Dataset-tools
================


## data_augmentation
This file get a folder with .jpg images and creates another folder inside 'augmentations' and saves augmented images in that folder.

To flip the images only 

```lua
th data_augmentation.lua -p '/your/path/to/folder' -ta 0 -ra 0
```

To translate and flip 
```lua
th data_augmentation.lua -p '/your/path/to/folder' -ta 15 -t 5 -r 0
```
ta sets the maximum translation value to 15, t sets the interval to 5. For every t step we crop an image.
With this configuration we crop (15/5)^2 = 9 images.

To just transtale 
```lua
th data_augmentation.lua -p '/your/path/to/folder' -h -ra 0
```

To just rotate
```lua
th data_augmentation.lua -p '/your/path/to/folder' -h -ta 0 ra 0.2 -r 2
```
ra sets the maximum rotation angle to 0.2 and r asks for 2 images to create.
With this configuration we create to 2 images by rotation the image 0.1 and 0.2.

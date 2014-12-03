Dataset-tools
================


## data_augmentation
This file get a folder with .jpg images and creates another folder inside 'augmentations' and saves augmented images in that folder.

To flip the images only 

```lua
th data_augmentation.lua -p '/your/path/to/folder' -j 0 
```

To translate and flip 
```lua
th data_augmentation.lua -p '/your/path/to/folder' -j 15
```

To just transtale 
```lua
th data_augmentation.lua -p '/your/path/to/folder' -h
```

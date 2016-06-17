# Torch Toolbox

A collection of snippets and libraries for Torch from Purdue [e-Lab](http://engineering.purdue.edu/elab/).

 - [Adversarial](Adversarial) to generate adversarial examples using fast gradient sign method
 - [Bincompat](Bincompat) to read and write Torch7's 64-bit file format on 32-bit platforms
 - [BN-absorber](BN-absorber) to merge batch normalization module into convolution layer
 - [Convert-MM](Convert-MM) to replace the ConvolutionMM with Convolution to support stride on CPU
 - [Face-align-Cao](Face-align-Cao) to find facial landmarks and align the face using regression model
 - [Face-align-Kazemi](Face-align-Kazemi) to find facial landmarks and align the face using regression trees
 - [Dataset-tools](Dataset-tools) to augment folder of images
 - [GPU-RAM](GPU-RAM) to monitor the GPU's memory usage
 - [GUI](GUI) provides some examples for playing with the *Qt Graphical User Interface*
 - [Net-toolkit](Net-toolkit/README.md) is a Lua package for smart/light network saving
 - [Re-plotting](Re-plotting) to recover bad plot and merging splitted training
 - [Sanitize](Sanitize) to free temporary buffers in order to reduce a model size before saving on disk
 - [Try-model](Try-model) to load a new model's architecture and visualise its memory consumption
 - [Tutorials](Tutorials) to help you set up Torch-related environment
   - [Setup an Intel Compute Stick](Tutorials/Setup-an-Intel-Compute-Stick.md)
   - [Setup an Ubuntu GPU box](Tutorials/Setup-an-Ubuntu-GPU-box.md)
   - [Recover a Jetson TK1 FS](Tutorials/Recover-filesystem-on-Jeson-TK1.md)
   - [Update a new Jetson TK1](Tutorials/Update-new-Jetson-TK1.md)
   - [Install CUDA on TK1](Tutorials/Install-CUDA-6.5-on-Jetson-TK1.md)
   - [Install CUDNN on TK1](Tutorials/Install-CUDNN-on-Jetson-TK1.md)
 - [Video-decoder](Video-decoder) to receive and decode video from various sources
 - [Video-transmitter](Video-transmitter) to encode and transmit video on the fly
 - [Weight-init](Weight-init) to reset weight and bias to have desired statistics
 - [demo-core](demo-core) core scripts for the demo-visor
 - [demo-visor](demo-visor) to process a video or images with a network
# Setup cuDNN library for Torch on Jetson Tegra K1

This tutorial assumes that NVIDIA toolkit (less than 6.5) and Torch are already installed on the board.


## Set up basic configurations on TK1 device

Now it is time to update CUDA compiler (>= 6.5).
Download the package from NVIDIA website on the board and install.

```sh
wget http://developer.download.nvidia.com/embedded/L4T/r21_Release_v3.0/cuda-repo-l4t-r21.3-6-5-prod_6.5-42_armhf.deb
sudo dpkg -i cuda-repo-l4t-r21.3-6-5-prod_6.5-42_armhf.deb
sudo apt-get update
sudo apt-get install cuda-toolkit-6-5
sudo usermod -a -G video ubuntu
echo ' ' >> ~/.bashrc
echo '# Cuda dependencies' >> ~/.bashrc
echo 'export PATH=/usr/local/cuda-6.5/bin:$PATH' >> ~/.bashrc
echo 'export LD_LIBRARY_PATH=/usr/local/cuda-6.5/lib:$LD_LIBRARY_PATH' >> ~/.bashrc
```

Then, set up basic configurations at your preference on TK1 device.
First, allow community-maintained open-source softwares.

```
sudo add-apt-repository universe
```

## Installing Torch and packages

Tutorial [here](Install-CUDA-6.5-on-Jetson-TK1.md).
You can easily install Torch with [ezinstall script](https://github.com/torch/ezinstall).

To install the `CUDNN` libraries and package, check the tutorial [here](Tutorials/Install-CUDA-6.5-on-Jetson-TK1.md).


## Test

Run test scripts in https://github.com/soumith/cudnn.torch or

```lua
require("cudnn")

local net = nn.Sequential()
net:add(cudnn.SpatialConvolution(3, 48, 5, 5, 1, 1, 0, 0))
net:add(cudnn.ReLU())
net:add(cudnn.SpatialMaxPooling(2, 2, 2, 2))
net = net:cuda()

local input = torch.Tensor(128, 3, 231, 231):cuda()
net:forward(input)
cutorch.synchronize()
```

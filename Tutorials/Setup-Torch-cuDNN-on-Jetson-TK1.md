## Setup cuDNN library for Torch on Jetson Tegra K1

This tutorial assumes that NVIDIA toolkit (less than 6.5) and Torch are already installed on the board.
You can easily install Torch with [ezinstall script](https://github.com/torch/ezinstall).


### Update NVIDIA driver

cuDNN requires NVIDIA toolkit >= 6.5.
To update driver and compiler, `ssh` into the TK1 board and download the latest driver package from NVIDIA website.

```sh
cd ~/NVIDIA-INSTALLER
wget http://developer.download.nvidia.com/mobile/tegra/l4t/r21.1.0/Tegra124_Linux_R21.1.0_armhf.tbz2
```

Then, change the NVIDIA-INSTALLER script so as to grab the latest driver you just downloaded.

```
sudo vim ~/NVIDIA-INSTALLER/installer.sh
```

Modify the filename in two spots as follows

```sh
cd "${LDK_DIR}"
echo "Extracting the BSP..."
# you are already root use so no 'sudo' required
sudo tar xjpmf Tegra124_Linux_R21.1.0_armhf.tbz2    # here
# check for errors
if [ $? -ne 0 ]; then
     echo "ERROR extracting Tegra124_Linux_R21.1.0_armhf.tbz2"     # and there
     exit 1
fi
cd -
```

Then run the script to update driver.
It may break your display after reboot.

```sh
sudo NVIDIA-INSTALLER/installer.sh 
sudo reboot
```


### Update NVIDIA compiler

Now it is time to update CUDA compiler (>= 6.5).
Download the package from NVIDIA website on the board and install.

```sh
wget http://developer.download.nvidia.com/compute/cuda/6_5/rel/installers/cuda-repo-l4t-r21.1-6-5-prod_6.5-14_armhf.deb
sudo dpkg -i cuda-repo-l4t-r21.1-6-5-prod_6.5-14_armhf.deb
sudo apt-get update
sudo apt-get install -y cuda-toolkit-6-5
sudo usermod -a -G video $USER
```

After installation, update `LD_LIBRARY_PATH` or run `sudo ldconfig` with proper configuration.
Since Linux for Tegra runs on 32-bit, the library path is `/usr/local/cuda/lib` as opposed to `lib64` on x86 64-bit machine.


### Rebuild CUDA-related Torch package

Rebuild your CUDA related Torch package with the command,

```sh
luarocks install cutorch
luarocks install cunn
luarocks install cudnn
```

FYI, `luarocks install cudnn` will install Torch bindings (written by Soumith, https://github.com/soumith/cudnn.torch) for cuDNN library.

cuDNN library is provided as a shared library file and can be downloaded from [NVIDA website](https://developer.nvidia.com/cuDNN), but you must be registered as a CUDA developer.
After download, you need to let the Torch binding know where the library is located by either temporarily setting LD_LIBRARY_PATH or moving the `libcudnn*` into trusted directory.
 
```sh
# download cudnn-6.5-linux-armv7-R1.tgz
tar xvf cudnn-6.5-linux-armv7-R1.tgz
```

To link the library,

```sh
mv cudnn-6.5-linux-armv7-R1/libcudnn* /usr/local/lib/    # for example
sudo ldconfig
```

or 

```sh
export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:/your/cudnn-6.5-linux-armv7-R1/directory/
```


### Test

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

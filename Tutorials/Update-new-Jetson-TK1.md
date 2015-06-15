# Update NVIDIA driver

`cuDNN` requires nVIDIA toolkit >= 6.5. Check tutorial [here](Install-CUDA-6.5-on-Jetson-TK1.md).
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

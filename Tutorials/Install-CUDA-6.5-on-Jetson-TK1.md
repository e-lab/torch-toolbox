# Installation guide of CUDA 6.5 on TK1

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

# Setup Torch on Intel Compute Stick

This installation tutorial targets the [Intel Compute Stick STCK1A32WFC](http://www.intel.com/content/www/us/en/compute-stick/intel-compute-stick.html) that originally comes with Windows 8.1.
Reference: http://liliputing.com/2015/07/ubuntu-on-the-32gb-intel-compute-stick-you-have-to-install-it-yourself.html


## Install Ubuntu

Prepare a boot sector with operating system (here we used ubuntu 14.04 64bit) on MicroSD card.
Plug the card into the device and set BIOS option (F2 or F10) to read the ubuntu image at bootup.
The rest is the same as regular ubuntu installation.


## Update graphic drivers

Download the latest driver for Intel Graphic card (http://01.org/linuxgraphics) or run the bash script below.

```bash
wget https://01.org/sites/default/files/skl_dmc_ver1_19.tar.bz2
tar xvf skl_dmc_ver1_19.tar.bz2
cd skl_dmcver1_19
chmod +x install.sh
sudo ./install.sh
```

Then install basic apps for your convenience.

```bash
sudo apt-get update
sudo apt-get install vim git tmux htop openssh-server -y
```


## Install Torch with custom OpenBLAS configuration

Since OpenBLAS does not support Intel Atom Silvermont architecture (https://github.com/xianyi/OpenBLAS/issues/548),
you need to manually specify the target CPU.
This is the only change for Torch installation and the rest is the same as generic installation.

Clone the ezinstall repository,

```bash
git clone http://github.com/torch/ezinstall.git
cd ezinstall
```

and apply git patch as follows,

```bash
echo '
diff --git a/install-deps b/install-deps
index 6520fb4..c3769aa 100755
--- a/install-deps
+++ b/install-deps
@@ -10,16 +10,16 @@ install_openblas() {
     git clone https://github.com/xianyi/OpenBLAS.git -b master
     cd OpenBLAS
     if [ $(getconf _NPROCESSORS_ONLN) = 1 ]; then
-        make NO_AFFINITY=1 USE_OPENMP=0 USE_THREAD=0
+        make NO_AFFINITY=1 USE_OPENMP=0 USE_THREAD=0 TARGET=NEHALEM
     else
-        make NO_AFFINITY=1 USE_OPENMP=1
+        make NO_AFFINITY=1 USE_OPENMP=1 TARGET=NEHALEM
     fi
     RET=$?; 
     if [ $RET -ne 0 ]; then
         echo "Error. OpenBLAS could not be compiled";
         exit $RET;
     fi
-    sudo make install
+    sudo make install TARGET=NEHALEM
     RET=$?; 
     if [ $RET -ne 0 ]; then
         echo "Error. OpenBLAS could not be installed";
' | git apply -
```

then start installation.

```bash
./install-all
```

Once everything is done, do not forget to give ownership of `/usr/local` back to the admin user so that luarocks installation no longer requires `sudo`.

```bash
sudo chown -R username:username /usr/local
sudo rm -rf .cache/luarocks
```


## (Optional) Install openCL driver on Linux OS

This is optional step to set up openCL environment on the compute stick.
although the processor on the stick does not have openCL device other than CPU.

Since the openCL driver for Intel with Linux OS is packaged together with Intel media server studio,
you need to download the pacakge from the website (follow the link below)

 - https://software.intel.com/en-us/articles/opencl-drivers
 - https://software.intel.com/en-us/intel-media-server-studio

After download, extract the tarball, copy libraries into the system path and reboot the system.

```bash
tar xvf mediaserverstudioessentials2015r6.tar.gz
cd MediaServerStudioEssentials2015R6
tar xvf SDK2015Production16.4.2.1.tar.gz
cd SDK2015Production16.4.2.1
cd Generic
tar xvf intel-linux-media-ocl_generic_16.4.2.1-39163_64bit.tar.gz
sudo ./install_media.sh
sudo reboot
```

Now it is all set, you can try a test code (originally from NVIDIA) to see if it finds openCL-compatible devices.

```bash
wget https://gist.githubusercontent.com/rmcgibbo/6314452/raw/752cb3d14170fa7defb1ed37e3c2ce286248bb95/clDeviceQuery.cpp
g++ -o clDeviceQuery -I/opt/intel/opencl/include -L/opt/intel/opencl clDeviceQuery.cpp -lOpenCL
./clDeviceQuery
```

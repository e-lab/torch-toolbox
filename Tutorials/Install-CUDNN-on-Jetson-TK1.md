# Install CUDNN on Jetson TK1

`cuDNN` library is provided as a shared library file and can be downloaded from [NVIDA website](https://developer.nvidia.com/cuDNN), but you must be registered as a CUDA developer.
After download, you need to let the Torch binding know where the library is located by either temporarily setting LD_LIBRARY_PATH or moving the `libcudnn*` into trusted directory.

```sh
# download cudnn-6.5-linux-armv7-R1.tgz
tar xvf cudnn-6.5-linux-armv7-R1.tgz
```

To link the library:

```sh
mv cudnn-6.5-linux-armv7-R1/lib* /usr/local/lib/
sudo ldconfig
```

Now, `luarocks install cudnn` will install Torch bindings (written by Soumith, https://github.com/soumith/cudnn.torch) for cuDNN library.

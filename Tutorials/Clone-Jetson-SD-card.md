# Clone Jetson SD card

## Set up the local machine

Follow the instructions of [Recover a Jetson TK1 FS](Recover-filesystem-on-Jeson-TK1.md) until the

```bash
lsusb | grep -i nvidia
```

command and corresponding affermative output.

## Cloning the SD card

`cd` into `bootloader`, and clone the `APP` partition

```bash
cd /tmp/l4t/Linux_for_Tegra/bootloader
sudo ./nvflash --read APP system.img --bl ardbeg/fastboot.bin --go
```

After about 1,000 s (15 minutes) it will finish saying `file received successfully`.
A file named `system.img` of approximately 8 GB will be created in the `bootloader` directory.

## Packaging

We can remove the `rootfs`, since we have our image, but we need to package the image with the L4T used for building the image.

```bash
cd /tmp/l4t
sudo rm -rf Linux_for_Tegra/rootfs
tar -czf my_backup.tar.gz Linux_for_Tegra
```

## Writing image

Get the *Tegra TK1* into *recovery mode*, as explained [here](Recover-filesystem-on-Jeson-TK1.md#recover-tk1-drivers-and-filesystem) until the `lsusb` command.
Extract the `tar` image. `cd` into the extracted folder and burn the image

```bash
tar -xf my_backup.tar.gz
cd Linux_for_Tegra
sudo ./flash.sh -r -S 14580MiB jetson-tk1 mmcblk0p1
```

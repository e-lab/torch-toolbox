# Ubuntu GPU box setup

This is a quick quide (checklist) of things you need to do to get a `Ubuntu GPU box` up and going.

 - [Make a USB bootable drive](#make-a-usb-bootable-drive)
 - Main drive **must** be `dev/sda`, *otherwise it fails*
 - **NO** *quickBoot / FastBoot* option in the *BIOS*, *otherwise you cannot reboot*
 - [Update *Ubuntu*'s packages, *otherwise you use old stuff*](#update-stuff)
 - [Install `git`, `vim`, `tmux`, `htop` and `tree`, *otherwise nothing works*](#install-essentials)
 - [Add *exFAT* file system read/write support](#add-exfat-fs-support)
 - [Update terminal's colours, configurations and substitute `<Caps lock>` with additional `<Ctrl>`](#better-configuration), *otherwise it looks and feels like s*\*\**t*
 - [Assign *static IP*](#assign-static-ip)
 - [Install a `ssh` server](#install-a-ssh-server)
 - [Install *CUDA Toolkit*](#install-cuda-toolkit)
 - [Update graphics driver](#update-graphics-driver)
 - [Edit Terminal's settings](#edit-terminals-settings)
 - [Install Torch7](#install-torch7)
 - [Change ownership of `usr/local`](#change-ownership-of-usrlocal)

## Make a USB bootable drive

This instructions apply to the OSX platform.

Open `Disk Utility` and choose `1 partition` to *format* as `Free Space`.
Convert the `iso` into a `dmg` with

```bash
hdiutil convert -format UDRW -o ubuntu-image ubuntu*desktop-amd64.iso
```

find out where the *USB* drive is mounted

```bash
diskutil list
```

(for example `/dev/diskNB`), and write the image on it

```bash
dd if=./ubuntu-image.dmg of=/dev/diskNB bs=1m
```

## Update stuff

```bash
sudo apt-get update
sudo apt-get dist-upgrade -y
sudo apt-get autoremove -y
```

## Install essentials

```bash
sudo apt-get install -y git vim tmux htop tree
```

## Add *exFAT* FS support

```bash
sudo apt-get install -y exfat-utils exfat-fuse
```

## Better configuration

Go [here](https://github.com/Atcold/Unix-dot-files), and go through it.

## Assign **static IP**

`Network Connection` -> `Edit...` -> `IPv4 Settings` -> `Method:` -> `Manual` -> `Add`.
To get the *IP address* of your *name* you can `ping` it in the terminal (`ping <myStaticName.ecn.purdue.edu>`).
Configurations for *DNS servers*, *gateway* and *sub-net mask* can be found on the corresponding [ECN webpage](https://engineering.purdue.edu/ECN/Support/KB/Docs/IPSettings).

## Install a `ssh` server

```bash
sudo apt-get install -y openssh-server
```

## Install *CUDA Toolkit*

Let's download the latest *CUDA Toolkit* available from [*nVIDIA* website](https://developer.nvidia.com/cuda-downloads) for your *Ubuntu* version. `ssh` remotely from another machine (or use a virtual console with `<Ctrl>`-`<Alt>`-`F1` to `F6`).

```bash
sudo service lightdm stop
cd Downloads
chmod +x cuda*
sudo ./cuda*
```

**DO NOT** install the *OpenGL* libraries if you are planning to use your integrated graphic card for displaying purpose.
Accept everything else and press enter for default locations.
Add the following lines to your `~/.bashrc`.

```bash
# CUDA
PATH=$PATH':/usr/local/cuda/bin'
LD_LIBRARY_PATH=$LD_LIBRARY_PATH':/usr/local/cuda/lib64'
```

You can try to run one of the *Samples* to test if everything went well. Before that, get the metapackage `build-essential` which will install `gcc` compiler and other related packages (`sudo apt-get install -y build-essential`). cd into `~/NVIDIA_CUDA*Samples/1_Utilities/deviceQuery`, `make` and then `./deviceQuery`.

`sudo reboot` your system.

## Update graphics driver

Check if your drivers are up to date (compare what you get with `nvidia-smi` with what you can find on [*nVIDIA* webpage](http://www.nvidia.com/Download/index.aspx)).
If they are not, update your system like you just did for installing the *CUDA Toolkit* but with the driver `run` package instead.

As mentioned before, if you are planning to use your integrated graphic card for displaying purpose, **DO NOT** install the *OpenGL* libraries nor update your `/etc/X11/xorg.conf`! In order to doing so you can digit (with the correct version numbering):

```bash
sudo service lightdm stop
cd Downloads
chmod +x NVIDIA*
sudo ./NVIDIA-Linux-x86_64-352.41.run  --no-opengl-files
sudo reboot
```

## Edit Terminal's settings

`Edit` -> `Profile Preferences` -> `Scrolling` -> check `Unlimited`.
## Install Torch7

```bash
curl -s https://raw.githubusercontent.com/torch/ezinstall/master/install-all | bash
```

## Change ownership of `usr/local`

Since `/usr/local` is belonging to the (administrator) user, we enforce this with

```bash
sudo chown -R me:me /usr/local
sudo rm -rf ~/.cache/luarocks
```

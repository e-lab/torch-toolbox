## Video Transmitter Library for Torch

A light version of video transmitter that utilizes avcodec library.
It encodes a sequence of images and transmit to the destination via UDP.


### Dependencies

This library requires libav both host and client computers.
On the host, to install libav tools on Linux,

```sh
sudo apt-get install -y libavformat-dev libavutil-dev libavcodec-dev
```

or brew ffmpeg on Mac,

```sh
brew install ffmpeg
```

On the client, [VLC player](http://www.videolan.org/vlc/) can be used to check transmitted stream since it uses libav library.


### Build

To build library,

```sh
make
```


### Test

Run the script on host computer with destination IP address.

```sh
th test.lua -v 123.123.123.123   # destination IP address
```

To see the transmitted stream, open VLC player on the client.
Click `open network` and `open RTP/UDP stream` then
set `UDP` protocol, `Unicast` mode, and port number `6970`.

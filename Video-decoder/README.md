## Video Decoder Library for Torch

A light version of video decoder that utilizes avcodec library.
It decodes a video and display image frames.


### Dependencies

This library requires libav. To install it on Linux,

```sh
sudo apt-get install -y libavformat-dev libavutil-dev libavcodec-dev
```

On Mac,

```sh
brew install ffmpeg
```


### Build

To build library,

```sh
make
```


### Test

It can decode a local video file

```sh
qlua test-frame.lua -v /path/to/your/video/video.mp4
```

and also HTTP streaming from IP camera by making the following change in `test-frame.lua`.

```lua
video.init("IP.ADDRESS.TO.IP.CAMERA", "mjpeg")
```

If you want to receive a stream and save as a file in background, run the testing script with the sample video stream.

```sh
wget http://www.live555.com/liveMedia/public/h264-in-mp2t/bipbop-gear1-all.ts   # downalod sample stream
qlua test-stream.lua -v rtsp://127.0.0.1/bipbop-gear1-all.ts
```

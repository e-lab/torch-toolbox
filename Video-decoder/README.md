## Video Decoder Library for Torch

A light version of video decoder that utilizes avcodec library.
It decodes a video and display image frames.


### Dependencies

This library requires libav. To install it on Linux,

```sh
sudo apt-get install -y libavformat-dev libavutil-dev libavcodec-dev libswscale-dev
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

# Library Lua API

## init

Opens a file/stream with libavformat

Parameters:

- file to open
- file format *optional* 

Returns:

- status (1=ok, 0=failed)
- width
- height
- number of present frames
- frame rate
	
Example:

    local status, height, width, length = video.init('http://10.184.37.212:8080/video', 'mjpeg')

## capture

Opens a video capture device with the videocap library. This function is only available on Linux.

Parameters:

- device path (in the form /dev/videoN)
- width
- height
- fps (optional, driver default in this case)
- number of buffers (optional, default 1)
- encoder path (in the form /dev/videoN, or just "auto", optional)
- encoding quality (suggested values: 20-30, bigger number is worse quality and shorter file, optional)

Returns:

- status (true=ok, false=failed)

Example:

    status = video.capture('/dev/video0', 1280, 720, 25)

## frame_rgb

Gets the next/last frame in RGB format from the file/stream/device

Parameters:

- tensor (has to be byte or float tensor, have dimension 3 and the first size has to be 3)

Returns:

- status (true=ok, false=failed)

Example:

    tensor = torch.ByteTensor(3, height, width)
    status = video.frame_rgb(tensor)
	
Note: frame_rgb takes the next received or captured frame. The caller has to process
the data fast enough (at the same rate of the stream/camera) or an overrun will occur.
If the stream or camera captured frames are read by the background thread started with
startremux, this is not necessary anymore, as this function will behave differently and
will just take the last received frame. If the caller cannot process the frames
fast enough and is not intentioned to save the fragments of the received or captured
video in any way, it's just enough to give startremux('dummyfilename', 'mp4', 0) after
init or capture. Nothing will be saved if savenow is not called.
	
## frame_yuv
	
Gets the next frame in YUV format from the file/stream.
Works in the same way as frame_rgb, but support for float tensors and startremux and capture is missing.

## frame_resized
	
Gets the next frame in RGB format from the file/stream/device and resize it to the tensor size

Parameters:

- tensor (has to be byte or float tensor, have dimension 3 and the first size has to be 3)

Returns:

- status (true=ok, false=failed)

Note: It works as frame_rgb, but the image is resized to the tensor size
and before being resized, is saved to a temporary buffer for subsequent JPEG encoding

## frame_batch_resized

If take is true, gets the next batch frames in RGB format from the file or stream,
otherwise the images are taken from the internal buffer
Rescale them to width x height and return them in a 4D tensor
batch can be max 32 because frames are buffered by libavcodec, which has 32 buffers

Parameters:

- batch
- width
- height
- take

Returns:

- 4D tensor of size (n, 3, height, width) or nil, where n is the minimum between batch and
  the number of read frames

## frame_jpeg

Encodes in JPEG the frame previously received with frame_resized

Returns:

- status (true=ok, false=nothing to encode (frame_resized never called))

Note: It will not be encoded again (the cached copy will be used) if frame_jpeg or
save_jpeg has been already called for that frame
	
## save_jpeg
	
Encodes in JPEG the frame previously received with frame_resized and saves it to filename

Parameters:

- filename

Returns:

- status (true=ok, false=nothing to encode (frame_resized never called))

Note: It will not be encoded again (the cached copy will be used) if frame_jpeg or
save_jpeg has been already called for that frame

Returns:

- status (true=ok, false=nothing to encode (frame_resized never called))

## exit

Stops and closes the decoder/video capture device/receiving thread

## startremux

Starts to receive from the stream opened with init or capture and starts to
write file fragments. fragment base path in the form A.B is changed to A_timestamp.B.
format is the file format (optional), if it cannot be deduced
from the file extension. During reception, frame_rgb can be used and it will take the latest received frame

Parameters:

- fragment base path
- format
- fragment size in seconds *optional*

If fragment size is not given or if it's zero, nothing is saved until a savenow command is given
If fragment size is -1, the function will generate a continuous stream (no fragmentation), to
be used when streaming to network (in this case fragment base path will not be changed)

Returns:

- status (1=ok, 0=failed)

Example:
	
	local status, height, width, length = video.init('http://10.184.37.212:8080/video.ts')
	video.startremux('video.ts', 'mpegts', 30)
    tensor = torch.ByteTensor(3, height, width)
    status = video.frame_rgb('torch.ByteTensor', tensor)
	
## savenow

Saves a portion of the received video around this moment.
Receiving should have been started with startremux (with 0 fragment size) before giving this command.
This routine triggers the receiving thread to save the buffered frames from at least now - seconds before
to now + seconds after. seconds before is an "at least" value, because saving always starts with a keyframe;
of course, it's less than this if there is not enough buffered data.

Parameters:

- seconds before
- seconds after
- filename together with the path

Returns:

- status (1=ok, 0=failed)

Example:
	
	local status, height, width, length = video.init('rtsp://username:password@192.168.0.65:88/videoMain')
	video.startremux('fragment.mp4', 'mp4')
	os.execute("sleep 20")
	video.savenow(5,5)

## stopremux

Stops to receive the stream started by startremux
	
Returns:

- status (1=ok, 0=failed)

## https_init

Initialize the SSL/TLS library

Parameters:

- certificate file used to authenticate the server
	
Returns:

- error code (0=ok, -6=SSL/TLS support not compiled in, -7=certificate file not found)

## loglevel

Set the logging level of the library

Parameters:

- loglevel

Does not return anything. 0 means that only errors are logged, higher (positive) numbers enables more logging

## diffimages

Compare two images

Parameters:

- image1 given as a float or byte tensor
- image2 given as a float or byte tensor (same size as image2)
- sensitivity: the minimum difference between two pixels in order to consider them different
- area: minimum fraction of total area with different pixels, defaults to 0.001

Returns true, if the images are different, otherwise returns false

Example:
	
	print(diffimages(image.lena(), image.lena(), 0.01, 0.001))

## encoderopen

Opens the libav video encoder

Parameters:

- format (tested formats are: mp4, avi and mpeg)
- file path
- frame width
- frame height
- fps

Returns: nothing

Example:

    video.encoderopen('mp4','test.mp4',512,512,25)
	video.encoderwrite(image.lena())
	video.encoderclose()

## encoderwrite

Sends a frame to the encoder

Parameters:

- frame

Returns: nothing

## encoderclose

No parameters

Returns: nothing

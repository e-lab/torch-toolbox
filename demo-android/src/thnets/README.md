# Stand-alone library for loading and running networks

## Installation

Requirements for library: OpenBLAS
Requirements for test: libpng and libjpeg

Make with "make"

Make options are DEBUG=1 and MEMORYDEBUG=0 (checks memory leaks) or 1 (generates full dump
of allocations in memdump.txt)

## Test program

	export LD_LIBRARY_PATH=/opt/OpenBLAS/lib:.
    ./test <path to directory with model files> <input file>

input file can be a .jpg or .png file, or a .t7 file containing a FloatTensor of dimension 3

## High level API description

### THNETWORK *THLoadNetwork(const char *path)

Loads the network contained in the path directory and returns a THNETWORK object or 0, if the
network cannot be loaded. The reason can be obtained with THLastError().

### void THMakeSpatial(THNETWORK *network)

Makes the loaded network suitable for images bigger of the eye size.

### int THProcessFloat(THNETWORK *network, float *data, int batchsize, int width, int height, float **result, int *outwidth, int *outheight)

Runs the network on the float data. Float data is organized as a coniguous array of
size batchsize x 3 x height x width, where 3 is the number of color planes.

Returns the number of categories in the output and the size of the output in outwidth and outheight.
result will point to the array with the data and *must* not be freed.
The data is a contiguous array of size batchsize x number of categories x outheight x outwidth.

### int THProcessImages(THNETWORK *network, unsigned char **images, int batchsize, int width, int height, int stride, float **result, int *outwidth, int *outheight)

Runs the network on the series of images. Images is an array with batchsize pointers and
each element points to the start of the image. Images are arrays of size
height x stride x 3, where only the first width of each line long stride contains data.

Returns the number of categories in the output and the size of the output in outwidth and outheight.
result will point to the array with the data and *must* not be freed.
The data is a contiguous array of size batchsize x number of categories x outheight x outwidth.

### void THFreeNetwork(THNETWORK *network)

Frees the network and all associated data and outputs.

### int THLastError()

Returns an error code describing the reason of the last error. It is now used only for
THLoadNetwork and can give these results:

- 0 Ok
- -1 The file cannot be opened
- -2 The file cannot be read till the end
- -3 The file contains some elements, which were not implemented in this library
- -4 The file is corrupted
- -5 The file contains torch objects not expected in that file

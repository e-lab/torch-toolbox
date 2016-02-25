UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)
LBITS := $(shell getconf LONG_BIT)
TORCH := $(shell if [ -e $(HOME)/torch/install/lib ] ; then echo $(HOME)/torch/install ; else echo /usr/local ; fi)

INCLUDE = -I. -I$(TORCH)/include
LDFLAGS := -lavutil -lavformat -lavcodec -lswscale -lssl
LIBOPTS = -shared -L$(TORCH)/lib
CFLAGS = -O3 -fopenmp -c -fpic -Wall -DINCLUDEOPENSSL -DUSE_SHA1
VPATH = fileio
VIDEODEC_FILES = video_decoder.o http.o mpjpeg.o
BINCOMPAT_FILES = bincompat.o
LUALIBFILES = libvideo_decoder.so
CC = gcc

ifneq ($(filter arm%,$(UNAME_P)),)
	CFLAGS += -mfpu=neon -DUSEGLES2
	LDFLAGS += /usr/lib/arm-linux-gnueabihf/libjpeg.so /usr/lib/arm-linux-gnueabihf/libpng.so
else
	ifneq ($(UNAME_S),Darwin)
		LDFLAGS += /usr/lib/x86_64-linux-gnu/libjpeg.so /usr/lib/x86_64-linux-gnu/libpng.so
	endif
endif

ifeq ($(UNAME_S),Linux)
	VIDEODEC_FILES += videocap.o videocodec.o
	CFLAGS += -DDOVIDEOCAP
endif

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT /usr/local/lib/libjpeg.dylib /usr/local/lib/libpng.dylib
	CFLAGS += -DDARWIN
endif

ifeq ($(LBITS),32)
	LUALIBFILES += libbincompat.so
endif

.PHONY : all
all : $(LUALIBFILES)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) $<

libvideo_decoder.so : $(VIDEODEC_FILES)
	$(CC) $(VIDEODEC_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

libbincompat.so : $(BINCOMPAT_FILES)
	$(CC) $(BINCOMPAT_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

install : $(LUALIBFILES)
	cp $(LUALIBFILES) ../

.PHONY : clean
clean :
	rm -f *.o libbincompat.so libvideo_decoder.so

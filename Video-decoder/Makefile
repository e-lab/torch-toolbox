UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

TORCH = $(HOME)/torch/install
INCLUDE = -I. -I$(TORCH)/include
LDFLAGS := -lavutil -lavformat -lavcodec -lswscale
LIBOPTS = -shared -L$(TORCH)/lib/lua/5.1 -L$(TORCH)/lib
CFLAGS = -O3 -c -fpic -Wall
VIDEODEC_FILES = video_decoder.o mpjpeg.o
CC = gcc

#Lua has its own libjpeg.so which confuses the linker if we simply use -ljpeg
ifneq ($(filter aarc% arm%,$(UNAME_P)),)
	LDFLAGS += /usr/lib/arm-linux-gnueabihf/libjpeg.so
else ifneq ($(filter x86_64,$(UNAME_P)),)
	ifneq ($(UNAME_S),Darwin)
		LDFLAGS += /usr/lib/x86_64-linux-gnu/libjpeg.so
	endif
else ifneq ($(filter i686,$(UNAME_P)),)
	LDFLAGS += /usr/lib/i386-linux-gnu/libjpeg.so
else
	LDFLAGS += -ljpeg -lpng
endif

ifneq ($(filter arm%,$(UNAME_P)),)
	CFLAGS += -mfpu=neon
endif

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT
endif

ifeq ($(UNAME_S),Linux)
	VIDEODEC_FILES += videocap.o videocodec.o
	CFLAGS += -DDOVIDEOCAP
endif

.PHONY : all
all : libvideo_decoder.so

.c.o :
	$(CC) $(CFLAGS) $(INCLUDE) $<

libvideo_decoder.so : $(VIDEODEC_FILES)
	$(CC) $(VIDEODEC_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

install : libvideo_decoder.so
	sudo cp libvideo_decoder.so /usr/local/lib/lua/5.1/

uninstall :
	sudo rm /usr/local/lib/lua/5.1/libvideo_decoder.so

.PHONY : clean
clean :
	rm -f *.o libvideo_decoder.so

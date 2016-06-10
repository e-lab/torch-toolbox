NEWFFMPEG = 0

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

TORCH = $(HOME)/torch/install
INCLUDE = -I. -I$(TORCH)/include
LDFLAGS := -lavutil -lavformat -lavcodec -lswscale
LIBOPTS = -shared -L$(TORCH)/lib/lua/5.1 -L$(TORCH)/lib
CFLAGS = -O3 -c -fpic -Wall
VIDEODEC_FILES = video_decoder.o mpjpeg.o
FASTIMAGE_FILES = fastimage.o
CC_FILES = 8cc.o
CC = gcc

#Lua has its own libjpeg.so which confuses the linker if we simply use -ljpeg
ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT /usr/local/lib/libjpeg.dylib /usr/local/lib/libpng.dylib
	CFLAGS += -DDARWIN
else ifneq ($(filter aarc% arm%,$(UNAME_P)),)
	LDFLAGS += /usr/lib/arm-linux-gnueabihf/libjpeg.so /usr/lib/arm-linux-gnueabihf/libpng.so
	CFLAGS += -mfpu=neon
else ifneq ($(filter x86_64,$(UNAME_P)),)
	LDFLAGS += /usr/lib/x86_64-linux-gnu/libjpeg.so /usr/lib/x86_64-linux-gnu/libpng.so
else ifneq ($(filter i686,$(UNAME_P)),)
	LDFLAGS += /usr/lib/i386-linux-gnu/libjpeg.so /usr/lib/i386-linux-gnu/libpng.so
else
	LDFLAGS += -ljpeg -lpng
endif

ifeq ($(UNAME_S),Linux)
	VIDEODEC_FILES += videocap.o videocodec.o
	CFLAGS += -DDOVIDEOCAP
endif

ifeq ($(NEWFFMPEG),1)
	CFLAGS += -DNEWFFMPEG
endif

.PHONY : all
all : libvideo_decoder.so fastimage.so lib8cc.so

.c.o :
	$(CC) $(CFLAGS) $(INCLUDE) $<

libvideo_decoder.so : $(VIDEODEC_FILES)
	$(CC) $(VIDEODEC_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

fastimage.so : $(FASTIMAGE_FILES)
	$(CC) $(FASTIMAGE_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

lib8cc.so : $(CC_FILES)
	$(CC) $(CC_FILES) $(LIBOPTS) -o $@ $(LDFLAGS)

install : libvideo_decoder.so
	cp libvideo_decoder.so fastimage.so $(TORCH)/lib/lua/5.1/
	cp lib8cc.so $(TORCH)/lib

uninstall :
	rm $(TORCH)/lib/lua/5.1/libvideo_decoder.so \
		$(TORCH)/lib/lua/5.1/fastimage.so \
		$(TORCH)/lib/lib8cc.so
	

.PHONY : clean
clean :
	rm -f *.o libvideo_decoder.so fastimage.so lib8cc.so

UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

TORCH = $(HOME)/torch/install
INCLUDE = -I. -I$(TORCH)/include
LDFLAGS := -lavutil -lavformat -lavcodec
LIBOPTS = -shared -L$(TORCH)/lib/lua/5.1 -L$(TORCH)/lib
CFLAGS = -O3 -c -fpic -Wall
CC = gcc

ifneq ($(filter arm%,$(UNAME_P)),)
	CFLAGS += -mfpu=neon
endif

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT
endif


.PHONY : all
all : libvideo_decoder.so


video_decoder.o :
	$(CC) $(CFLAGS) $(INCLUDE) video_decoder.c


libvideo_decoder.so : video_decoder.o
	$(CC) $< $(LIBOPTS) -o $@ $(LDFLAGS)


install : libvideo_decoder.so
	sudo cp libvideo_decoder.so /usr/local/lib/lua/5.1/

uninstall :
	sudo rm /usr/local/lib/lua/5.1/libvideo_decoder.so

.PHONY : clean
clean :
	rm -f *.o libvideo_decoder.so

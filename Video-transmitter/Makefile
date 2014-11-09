UNAME_S := $(shell uname -s)
UNAME_P := $(shell uname -p)

INCLUDE = -I. -I/usr/local/include
LDFLAGS := -lavutil -lavformat -lavcodec
LIBOPTS = -shared -L/usr/local/lib/lua/5.1 -L/usr/local/lib/
CFLAGS = -O3 -fopenmp -c -fpic -Wall
CC = gcc

ifneq ($(filter arm%,$(UNAME_P)),)
	CFLAGS += -mfpu=neon
endif

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT
endif


.PHONY : all
all : libvideo_transmitter.so


video_transmitter.o :
	$(CC) $(CFLAGS) $(INCLUDE) video_transmitter.c


libvideo_transmitter.so : video_transmitter.o
	$(CC) $< $(LIBOPTS) -o $@ $(LDFLAGS)


install : libvideo_transmitter.so
	sudo cp libvideo_transmitter.so /usr/local/lib/lua/5.1/

uninstall :
	sudo rm /usr/local/lib/lua/5.1/libvideo_transmitter.so

.PHONY : clean
clean :
	rm -f *.o libvideo_transmitter.so

UNAME_S := $(shell uname -s)
LBITS := $(shell getconf LONG_BIT)

INCLUDE = -I. -I/usr/local/include -I$(HOME)/torch/install/include
LIBOPTS = -shared -L/usr/local/lib/lua/5.1 -L/usr/local/lib
CFLAGS = -O3 -fopenmp -c -fpic -Wall
LIBFILES =
CC = gcc

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT
	CFLAGS += -DDARWIN
endif

ifeq ($(LBITS),32)
	LIBFILES += libbincompat.so
endif


.PHONY : all
all : $(LIBFILES)

.c.o:
	$(CC) $(CFLAGS) $(INCLUDE) $<

libbincompat.so : bincompat.o
	$(CC) bincompat.o $(LIBOPTS) -o $@ $(LDFLAGS)


install : $(LIBFILES)
	cp $(LIBFILES) /usr/local/lib/lua/5.1/

uninstall :
	rm /usr/local/lib/lua/5.1/libbincompat.so


.PHONY : clean
clean :
	rm -f *.o $(LIBFILES)

# Can be no, 0 or 1
MEMORYDEBUG = no
# Can be 0 or 1
DEBUG = 0

CFLAGS = -Wall -c -fopenmp -fPIC
CC = gcc
VPATH = modules
LIBOBJS = thload.o thbasic.o thapi.o SpatialConvolutionMM.o SpatialMaxPooling.o Threshold.o \
	View.o SoftMax.o Linear.o Dropout.o SpatialZeroPadding.o Reshape.o images.o

ifneq ($(MEMORYDEBUG),no)
	LIBOBJS += memory.o
	CFLAGS += -DMEMORYDEBUG=$(MEMORYDEBUG)
endif

ifeq ($(DEBUG),1)
	CFLAGS += -g
else
	CFLAGS += -O3
endif

.PHONY : all
all : thnets.so test

*.o: thnets.h

.c.o:
	$(CC) $(CFLAGS) $<

thnets.so: $(LIBOBJS)
	$(CC) -o $@ $(LIBOBJS) -shared -fopenmp -L/opt/OpenBLAS/lib -lopenblas -lm -lpng -ljpeg

test: $(LIBOBJS) test.o
	$(CC) -o $@ test.o thnets.so

.PHONY : clean
clean :
	rm -f *.o thnets.so test

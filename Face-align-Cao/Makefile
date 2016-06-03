UNAME_S := $(shell uname -s)

TORCH = $(HOME)/torch/install
CXXFLAGS = -O3 -c -fpic -Wall -I/usr/include/opencv -I/usr/local/include -std=c++11 -I$(TORCH)/include
LDFLAGS := -shared
FILES = FernCascade.o Fern.o ShapeRegressor.o Utils.o face_align.o

LDFLAGS += -lopencv_calib3d -lopencv_contrib -lopencv_core -lopencv_features2d \
	-lopencv_flann -lopencv_gpu -lopencv_highgui -lopencv_imgproc \
	-lopencv_legacy -lopencv_ml -lopencv_objdetect -lopencv_ocl \
	-lopencv_photo -lopencv_stitching -lopencv_superres -lopencv_ts \
	-lopencv_video -lopencv_videostab -L/usr/local/lib/lua/5.1 -L/usr/local/lib

ifeq ($(UNAME_S),Darwin)
	LDFLAGS += -lTH -lluajit -lluaT
endif


libface_align.so: $(FILES) FaceAlignment.h
	$(CXX) $(FILES) -o $@ $(LDFLAGS)

install: libface_align.so
	cp libface_align.so /usr/local/lib/lua/5.1

uninstall:
	rm /usr/local/lib/lua/5.1/libface_align.so

clean:
	rm -f *.o libface_align.so

TORCH := $(shell if [ -e $(HOME)/torch/install/lib ] ; then echo $(HOME)/torch/install ; else echo /usr/local ; fi)
CXXFLAGS = -O3 -c -fPIC -Wall -I./dlib -I/opt/X11/include -I/usr/local/include -I$(TORCH)/include
LDFLAGS = -shared
FILES = dlib/dlib/all/source.o face_align.o
LDFLAGS += -L$(TORCH)/lib/lua/5.1 -L/usr/local/lib -L$(TORCH)/lib -L/opt/X11/lib -lTH -lluajit -lluaT -lX11


libface_align.so: $(FILES)
	$(CXX) $(FILES) -o $@ $(LDFLAGS)
	
install: libface_align.so
	cp libface_align.so ../

clean:
	rm -f *.o libface_align.so

# download and extract dlib for both os x and ubuntu
if [ ! -d "libface/dlib" ]; then
   wget http://dlib.net/files/dlib-18.18.tar.bz2
   bzip2 -d dlib-18.18.tar.bz2
   tar -xf dlib-18.18.tar
   mv dlib-18.18 libface/dlib

   rm -f dlib-18.18.tar.bz2
   rm -f dlib-18.18.tar
fi


# download and extract face detector parameters
if [ ! -f "shape_predictor_68_face_landmarks.dat" ]; then
   wget "http://sourceforge.net/projects/dclib/files/dlib/v18.10/shape_predictor_68_face_landmarks.dat.bz2"
   bzip2 -d shape_predictor_68_face_landmarks.dat.bz2

   rm -f shape_predictor_68_face_landmarks.dat.bz2
fi


# build libraries
cd libface
make install
cd ../libvideo
make install
cd ..


# more cleaning
rm -f libface/dlib


# error check
if [ ! -f "shape_predictor_68_face_landmarks.dat" ]; then
   echo "==> ERROR: failed to download shape_predictor_68_face_landmarks.dat"
fi
if [ ! -f "libface_align.so" ]; then
   echo "==> ERROR: failed to build the face-align library"
fi
if [ ! -f "libvideo_decoder.so" ]; then
   echo "==> ERROR: failed to build the video processing library"
fi

echo "==> Setup has been completed."
echo "==> Launch 'qlua test-*.lua' unless you found ERRORs."

## FaceAligment-Torch7 binding

The main algorithm is presented by Xudong Cao et al.
in "Face Alignment by Explicit Shape Regression" (CVPR 2012).
http://research.microsoft.com/pubs/192097/cvpr12_facealignment.pdf

And the algorithm source code is reimplemented by Bi Sai.
https://github.com/soundsilence/FaceAlignment


### Dependencies

Only requirement is openCV and can be installed by, for example on Ubuntu

```bash
sudo apt-get install libopencv-dev
```

or on MAC,

```bash
brew tap homebrew/science
brew install opencv
```


### Build

To build and install the binding, you need to

```bash
make
```

or

```bash
make install
```


### Test

The algorithm requires a pre-trained model.
The model pretrained on COFW dataset can be downloaded from [here]
(https://drive.google.com/file/d/0B0tUTCaZBkccOGZTcjJNcDMwa28/edit?usp=sharing).

and place `model.txt` in your working directory.
Then, run the testing script.

```bash
qlua test-camera.lua    # use camera input
qlua test-image.lua     # use static image
qlua test-dataset.lua   # convert dataset
```

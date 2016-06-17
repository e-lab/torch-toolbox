# demo vision application

This demo uses demo-core to process some input data with a network.
The network can be loaded in two formats:

1) A single binary torch file containing a table with these fields:

- net (the network)
- labels (a table with the labels of the categories of the network)
- mean (a vector with three floats to be subtracted from the input, optional)
- std (a vector with the three floats that divide the input, optional)

2) A directory with three files:

- model.net a binary torch file containing the network
- categories.txt a text file with the names of the categories; the first line
is supposed to be a label and will be ignored; if a comma is present in the line,
only the text till the comma will be considered
- stat.t7 a binary torch file containing a table with two fields, mean and std (optional)

The input can be an image, a directory filled with images, a video file or a device
name in the form cam0, cam1...

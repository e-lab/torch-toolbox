## Neural network model sanitizer (deprecated)

> After the PR https://github.com/torch/nn/pull/526 ,
> native `clearState()` method does clear intermediate buffers, so you no longer need this snippet.


During forward/backward propagation, some of the nn/cunn modules utilize
temporary buffers. Those buffers have no meaninful information after training
and saving such model wastes too much of disk space. This script can be used
to free unnecessary buffers in the model before you save it onto disk.


### Example

Refer to or run the script [example.lua](example.lua).

## Different weight intialization methods

This script helps you try different weight initialization methods.
The weights and biases in the network model will be reset based on the method provided.


```lua
-- design model
require('nn')
local model = nn.Sequential()
model:add(nn.SpatialConvolutionMM(3,4,5,5))

-- reset weights
local method = 'xavier'
local model_new = require('w-init')(model, method)
```

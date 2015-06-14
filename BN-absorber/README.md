## Batch normalization absorber

Batch normalization applies linear transformation to input in evaluation phase.
It can be absorbed in following convolution layer by manipulating its weights and biases.
The manipulation gives the same numerical result unless zeropadding is used in convolution layer.


### Example

Run the script `th example.lua` or refer to the code below.

```lua
require('nn')

-- prepare input
local x = torch.randn(2, 3, 5, 5)

-- set dummy model
local model = nn.Sequential()
model:add(nn.SpatialConvolutionMM(3, 10, 3, 3))
model:add(nn.ReLU())
model:add(nn.SpatialBatchNormalization(10, nil, nil, false))
model:add(nn.SpatialConvolutionMM(10, 10, 3, 3))
model:add(nn.ReLU())
model:add(nn.View(10))
model:add(nn.BatchNormalization(10, nil, nil, false))
model:add(nn.Linear(10, 10))
model:evaluate()

-- assign non-trivial initial values to BN
model.modules[3].running_mean:rand(10)
model.modules[3].running_std:rand(10):abs()
model.modules[7].running_mean:rand(10)
model.modules[7].running_std:rand(10):abs()

-- absorb batch normalization into convolution layer
local inplace = false
local model_wo_BN = require('BN-absorber')(model, inplace)

-- evaluate each model
local y     = model:forward(x)
local y_hat = model_wo_BN:forward(x)

-- measure numerical error (avg)
print('==> before')
print(model)
print('==> after')
print(model_wo_BN)
print('==> Avg Error: ', (y-y_hat):abs():mean())
```

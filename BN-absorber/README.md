## Batch normalization absorber

Batch normalization applies linear transformation to input in evaluation phase.
It can be absorbed in following convolution layer by manipulating its weights and biases.


### Example

Run the script `th example.lua` or refer to the code below.

```lua
require('nn')

-- prepare input
local x = torch.randn(2, 3, 5, 5)

-- set dummy model
local model = nn.Sequential()
model:add(nn.SpatialConvolutionMM(3, 10, 3, 3))
model:add(nn.SpatialBatchNormalization(10))
model:add(nn.ReLU())
model:add(nn.SpatialConvolutionMM(10, 10, 3, 3))
model:add(nn.SpatialBatchNormalization(10))
model:add(nn.ReLU())
model:add(nn.View(10))
model:add(nn.Linear(10, 10))
model:add(nn.BatchNormalization(10))
model:evaluate()

-- assign non-trivial initial values to BN
for _,i in ipairs{2, 5, 9} do
   model.modules[i].running_mean:rand(10)
   model.modules[i].running_std:rand(10):abs()
   model.modules[i].weight:rand(10)
   model.modules[i].bias:rand(10)
end

-- absorb batch normalization into convolution layer
local model_wo_BN = require('BN-absorber')(model:clone())

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

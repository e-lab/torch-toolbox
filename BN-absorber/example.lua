require('nn')

-- prepare input
local x = torch.randn(2, 3, 5, 5)


-- set dummy model
local model = nn.Sequential()
model:add(nn.SpatialConvolutionMM(3, 10, 3, 3))
model:add(nn.SpatialBatchNormalization(10, 1e-7))
model:add(nn.ReLU())
model:add(nn.SpatialConvolutionMM(10, 10, 3, 3))
model:add(nn.SpatialBatchNormalization(10, 1e-7))
model:add(nn.ReLU())
model:add(nn.View(10))
model:add(nn.Linear(10, 10))
model:add(nn.BatchNormalization(10))
model:evaluate()


-- assign non-trivial initial values to BN
for i = 1, #model.modules do
   local l = model.modules[i]
   if l.running_mean then l.running_mean:randn(l.running_mean:size()) end
   if l.running_std  then l.running_std:rand(l.running_std:size())    end
   if l.running_var  then l.running_var:rand(l.running_var:size())    end
   if l.weight       then l.weight:randn(l.weight:size())             end
   if l.bias         then l.bias:randn(l.bias:size())                 end
end


-- absorb batch normalization into convolution layer
local model_wo_BN = require('BN-absorber')(model:clone())


-- evaluate each model
local y     = model:forward(x)
local y_hat = model_wo_BN:forward(x)
local diff  = (y-y_hat):abs()

-- measure numerical error (avg)
print('==> before')
print(model)
print('==> after')
print(model_wo_BN)
print('==> Error: ', diff:mean(), diff:max())

require('torch')
require('nn')
local sanitize = require('sanitize')


-- define model
local model = nn.Sequential()
model:add(nn.SpatialConvolutionMM(3, 16, 5, 5))
model:add(nn.SpatialBatchNormalization(16, 1e-3))
model:add(nn.SpatialMaxPooling(2, 2, 2, 2))


-- set input
local x = torch.Tensor(128, 3, 32, 32)


-- compute dummy forward-backward
local y = model:forward(x)
local dx = model:backward(x, y)


-- save model
torch.save('model.t7', model)
torch.save('model-cleaned.t7', sanitize(model))


-- chk filesize
os.execute('du -sh model.t7')
os.execute('du -sh model-cleaned.t7')


-- test cleaned model if it still works
local model_cleaned  = torch.load('model-cleaned.t7')
model_cleaned:forward(x)


-- remove temp files
os.execute('rm -f model.t7')
os.execute('rm -f model-cleaned.t7')

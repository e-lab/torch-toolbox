--[[Author: Aysegul Dundar (adundar@purdue.edu)
-- This file converts Linear layers in the network to ConvolutionMM
-- so in the demo, you can step in the image.
--   Please update the input size based on your network.
-- ]]

require 'nnx'

if not arg[1] then print "Network unspecified (type it after the program's name)" return
else print('Loading: ' .. arg[1]) end
local network = torch.load(arg[1]):float()

local input = torch.Tensor(3, 149, 149):float()

torch.setdefaulttensortype('torch.FloatTensor')
local new_network = nn.Sequential()

function convert(network)
   for i=1, #network.modules do
      if network.modules[i].__typename == 'nn.Sequential' then
         convert(network.modules[i])
      else
         if network.modules[i].__typename == 'nn.Reshape' then
            -- do nothing
         elseif network.modules[i].__typename == 'nn.Linear' then
            
            if (#input:size() == 3) then 
               tmp_module = nn.SpatialConvolutionMM(input:size(1), network.modules[i].weight:size(1), input:size(2), input:size(3))
            else 
               tmp_module = nn.SpatialConvolutionMM(network.modules[i].weight[1], network.modules[i].weight[2], 1, 1)
            end
            tmp_module.weight:copy(network.modules[i].weight):resize(tmp_module.weight:size())
            tmp_module.bias:copy(network.modules[i].bias)

            new_network:add(tmp_module)

            input = tmp_module:forward(input)

         elseif network.modules[i].__typename == 'nn.Dropout' then
            -- do nothing
         else
            new_network:add(network.modules[i])
            input = network.modules[i]:forward(input)
         end
      end
   end
end
convert(network)
torch.save('new_network.net', new_network)

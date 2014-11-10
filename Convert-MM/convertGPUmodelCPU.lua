--[[Author: Aysegul Dundar (adundar@purdue.edu)
-- This file converts GPU trained ConvolutionMM module to Spatial Convolution
-- Because ConvolutionMM for GPU has the ability of having stride more than 1
-- whereas ConvolutionMM for CPU has not.
-- ]]

require 'cutorch'
require 'cunn'
require 'nnx'

if not arg[1] then print "Network unspecified (type it after the program's name)" return
else print('Loading: ' .. arg[1]) end
local network = torch.load(arg[1])

local new_network = nn.Sequential()

function convert(network)
   print(#network.modules)
   for i=1, #network.modules do
      if network.modules[i].__typename == 'nn.Sequential' then
         convert(network.modules[i])
      else
         if network.modules[i].__typename== 'nn.SpatialConvolutionMM' then
            if (network.modules[i].padding > 0) and network.modules[i].dW>1 then
               local pd = network.modules[i].padding
               new_network:add(nn.SpatialZeroPadding(pd, pd, pd, pd))
               local tmp = network.modules[i]
               local conv_tmp = nn.SpatialConvolution(tmp.nInputPlane, tmp.nOutputPlane, tmp.kW, tmp.kH, tmp.dW, tmp.dH)
               conv_tmp.weight = tmp.weight:float():reshape(tmp.nOutputPlane, tmp.nInputPlane, tmp.kW, tmp.kH)
               conv_tmp.bias   = tmp.bias:float()
               new_network:add(conv_tmp)
            else
               new_network:add(network.modules[i])
            end
         elseif network.modules[i].__typename == 'nn.Dropout' then
            -- do nothing
         else
            new_network:add(network.modules[i])
         end
      end
   end
end
convert(network)
torch.save('new_network.net', new_network:float())

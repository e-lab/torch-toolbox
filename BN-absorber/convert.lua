require 'cutorch'
require 'cunn'
require 'nnx'
ba = require 'BN-absorber'

if not arg[1] then print "Network unspecified (type it after the program's name)" return
else print('Loading: ' .. arg[1]) end
local network = torch.load(arg[1])

network = ba(network)
ba(network)
torch.save('model.net', network:float())

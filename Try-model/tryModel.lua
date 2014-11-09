--------------------------------------------------------------------------------
-- Routing for testing models.lua
-- Alfredo Canziani, Mar 2014
--------------------------------------------------------------------------------

-- Options ---------------------------------------------------------------------
lapp = require 'pl.lapp'

opt = lapp[[
--width     (number)       Input pixel side (128, 200)
--dataset   (string)       Training dataset (indoor51|imagenet)
--memMB                    Estimate RAM usage
--batchSize (default 128)  Batch size
--cuda      (default true) GPU
--dropout   (default 0.5 ) Dropout
--inputDO   (default 0   ) Input dropout
]]

opt.ncolors        = 3
opt.subsample_name = opt.dataset
opt.step           = false
opt.verbose        = true

torch.setdefaulttensortype('torch.FloatTensor')

-- Requires --------------------------------------------------------------------
require 'nnx'
if opt.cuda then
   if step then
      io.write('Requiring cunn')
      io.read()
   end
   require 'cunn'
end
require 'eex'
data_folder = eex.datasetsPath() .. 'originalDataset/'

-- Requiring models-------------------------------------------------------------
package.path = package.path .. ';../../Train/?.lua'

if step then
   io.write('Requiring classes')
   io.read()
end
require 'Data/indoor-classes'

if step then
   io.write('Requiring model')
   io.read()
end
statFile = io.open('.stat','w+')
require 'models'

model, loss, dropout, memory = get_model1()

io.close(statFile)
io.write('\nTemporary files will be deleted now. Press <Return> ')
io.read()
os.execute('rm .stat .pltStat .pltStatData')
print(' > Temporary files removed\n')

if opt.memMB then
   -- Conversion function ---------------------------------------------------------
   function inMB(mem)
      mem[0]=mem[0]*4/1024^2
      for a,b in pairs(mem.submodel1.val) do mem.submodel1.val[a] = b*4/1024^2 end
      for a,b in pairs(mem.submodel2.val) do mem.submodel2.val[a] = b*4/1024^2 end
      mem.parameters = mem.parameters*4/1024^2
   end

   inMB(memory)

   -- Plotting memory weight ------------------------------------------------------
   print(string.format("The network's weights weight %0.2f MB", memory.parameters))

   -- Allocating space
   mem = torch.Tensor(1 + #memory.submodel1.str+1 + #memory.submodel2.str+1)
   x = torch.linspace(1,mem:size(1),mem:size(1))

   -- Serialise <memory> table
   i=1; labels = '"OH" ' .. i -- OverHead
   mem[i] = memory[0]

   i=2; labels = labels .. ', "OH1" ' .. i -- OverHead <submodel1>
   mem[i] = memory.submodel1.val[0]
   for a,b in ipairs(memory.submodel1.str) do -- Building xtick labels
      i = i + 1
      labels = labels .. ', "' .. b .. '" ' .. i
   end
   mem[{ {3,3+#memory.submodel1.str-1} }] = torch.Tensor(memory.submodel1.val)

   i = i + 1; labels = labels .. ', "OH2" ' .. i -- OverHead <submodel2>
   mem[i] = memory.submodel2.val[0]
   for a,b in ipairs(memory.submodel2.str) do -- Building xtick labels
      i = i + 1
      labels = labels .. ', "' .. b .. '" ' .. i
   end
   mem[{ {3+#memory.submodel1.str+1,mem:size(1)} }] = torch.Tensor(memory.submodel2.val)

   print(string.format('The network training will allocate up to %.2d MB', mem:sum()))
   -- Plotting
   gnuplot.plot('Memory usage [MB]', x, mem, '|')
   gnuplot.raw('set xtics (' .. labels .. ')')
   gnuplot.axis{0,mem:size(1)+1,0,''}
   gnuplot.grid(true)
end

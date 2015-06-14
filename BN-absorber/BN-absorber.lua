local function manipulate_BN_spatial(model, i_cv, i_bn)
   -- get pointers
   local nb_input  = model.modules[i_cv].nInputPlane
   local nb_output = model.modules[i_cv].nOutputPlane
   local kH = model.modules[i_cv].kH
   local kW = model.modules[i_cv].kW
   local w = model.modules[i_cv].weight:view(nb_output, nb_input*kH*kW)
   local b = model.modules[i_cv].bias
   local mean = model.modules[i_bn].running_mean:view(nb_input, 1)
   local std  = model.modules[i_bn].running_std:view(nb_input, 1)

   -- manipulate
   w:cmul(torch.repeatTensor(std, nb_output, kH*kW))
   b:addmv(w, -torch.repeatTensor(mean, 1, kH*kW):view(nb_input*kH*kW))

   -- remove BN
   model:remove(i_bn)
end


local function manipulate_BN_linear(model, i_cv, i_bn)
   -- get pointers
   local nb_input  = model.modules[i_cv].weight:size(2)
   local nb_output = model.modules[i_cv].weight:size(1)
   local w = model.modules[i_cv].weight
   local b = model.modules[i_cv].bias
   local mean = model.modules[i_bn].running_mean
   local std  = model.modules[i_bn].running_std

   -- manipulate
   w:cmul(torch.repeatTensor(std, nb_output, 1))
   b:addmv(w, -mean)

   -- remove BN
   model:remove(i_bn)
end


local function BN_absorber(model_old, inplace)
   local inplace = inplace or false
   local model = (inplace and model_old) or model_old:clone()
   local BN = {
      index = 0,
      spatial = true,
      dim = 0,
   }
   local offset = 0

   for i = 1, #model do
      i = i - offset

      -- (1) mark if BN
      if model.modules[i].__typename == 'nn.SpatialBatchNormalization' then
         BN.index = i
         BN.spatial = true
         BN.dim = model.modules[i].running_mean:nElement()
      elseif model.modules[i].__typename == 'nn.BatchNormalization' then
         BN.index = i
         BN.spatial = false
         BN.dim = model.modules[i].running_mean:nElement()


      -- (2) absorb BN into CV
      elseif (model.modules[i].__typename == 'nn.SpatialConvolution') or
             (model.modules[i].__typename == 'nn.SpatialConvolutionMM') then
         if (BN.index > 0) and BN.spatial and (BN.dim == model.modules[i].nInputPlane) then
            manipulate_BN_spatial(model, i, BN.index)
            BN.index = 0
            offset = offset + 1
         end
      elseif model.modules[i].__typename == 'nn.Linear' then
         if (BN.index > 0) and not BN.spatial and (BN.dim == model.modules[i].weight:size(2)) then
            manipulate_BN_linear(model, i, BN.index)
            BN.index = 0
            offset = offset + 1
         end

      -- (3) set behavior for others
      elseif (model.modules[i].__typename == 'nn.SpatialMaxPooling') or
             (model.modules[i].__typename == 'nn.Dropout') then
         -- do nothing except Dropout v1
      elseif (model.modules[i].__typename == 'nn.View') or
             (model.modules[i].__typename == 'nn.Reshape') or
             (model.modules[i].__typename == 'nn.ReLU') or
             (model.modules[i].__typename == 'nn.Threshold') or
             (model.modules[i].__typename == 'nn.SoftMax') or
             (model.modules[i].__typename == 'nn.LogSoftMax') or
             (model.modules[i].__typename == 'nn.SpatialAveragePooling') then
         -- prev BN index not needed
         BN.index = 0
      end
      collectgarbage()
   end

   return model
end


return BN_absorber

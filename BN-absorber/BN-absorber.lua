require('nn')


local function absorb_bn(w, b, mean, std, affine, gamma, beta)
   w:cmul(std:view(w:size(1),1):repeatTensor(1,w:size(2)))
   b:add(-mean):cmul(std)

   if affine then
      w:cmul(gamma:view(w:size(1),1):repeatTensor(1,w:size(2)))
      b:cmul(gamma):add(beta)
   end
end


local function BN_absorber(x)
   local i = 1
   while (i <= #x.modules) do
      if x.modules[i].__typename == 'nn.Sequential' then
         BN_absorber(x.modules[i])
      elseif x.modules[i].__typename == 'nn.Parallel' then
         BN_absorber(x.modules[i])
      elseif x.modules[i].__typename == 'nn.Concat' then
         BN_absorber(x.modules[i])
      elseif x.modules[i].__typename == 'nn.DataParallel' then
         BN_absorber(x.modules[i])
      elseif x.modules[i].__typename == 'nn.ModelParallel' then
         BN_absorber(x.modules[i])
      else
         -- check BN
         if x.modules[i].__typename == 'nn.SpatialBatchNormalization' or
            x.modules[i].__typename == 'nn.BatchNormalization' then
            if x.modules[i-1] and
              (x.modules[i-1].__typename == 'nn.Linear' or
               x.modules[i-1].__typename == 'nn.SpatialConvolution' or
               x.modules[i-1].__typename == 'nn.SpatialConvolutionMM') then
               -- force weight to be in 2-dim
               local weight = x.modules[i-1].weight
               weight = weight:view(weight:size(1), weight:nElement()/weight:size(1))

               -- remove BN
               absorb_bn(weight,
                         x.modules[i-1].bias,
                         x.modules[i].running_mean,
                         x.modules[i].running_std,
                         x.modules[i].affine,
                         x.modules[i].weight,
                         x.modules[i].bias)
               x:remove(i)
               i = i - 1
            else
               assert(false, 'Convolution module must exist right before batch normalization layer')
            end
         end
      end
      i = i + 1
   end

   collectgarbage()
   return x
end


return BN_absorber

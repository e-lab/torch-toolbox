require('nn')


local absorb_bn_conv = function (w, b, mean, invstd, affine, gamma, beta)
   w:cmul(invstd:view(w:size(1),1):repeatTensor(1,w:nElement()/w:size(1)))
   b:add(-mean):cmul(invstd)

   if affine then
      w:cmul(gamma:view(w:size(1),1):repeatTensor(1,w:nElement()/w:size(1)))
      b:cmul(gamma):add(beta)
   end
end


local absorb_bn_deconv = function (w, b, mean, invstd, affine, gamma, beta)
   w:cmul(invstd:view(b:size(1),1):repeatTensor(w:size(1),w:nElement()/w:size(1)/b:nElement()))
   b:add(-mean):cmul(invstd)

   if affine then
      w:cmul(gamma:view(b:size(1),1):repeatTensor(w:size(1),w:nElement()/w:size(1)/b:nElement()))
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
      elseif x.modules[i].__typename == 'nn.ConcatTable' then
         BN_absorber(x.modules[i])
      else
         -- check BN
         if x.modules[i].__typename == 'nn.SpatialBatchNormalization' then
            if x.modules[i-1] and
              (x.modules[i-1].__typename == 'nn.SpatialConvolution' or
               x.modules[i-1].__typename == 'nn.SpatialConvolutionMM') then
               absorb_bn_conv(x.modules[i-1].weight,
                              x.modules[i-1].bias,
                              x.modules[i].running_mean,
                              x.modules[i].running_var:clone():sqrt():add(x.modules[i].eps):pow(-1),
                              x.modules[i].affine,
                              x.modules[i].weight,
                              x.modules[i].bias)
               x:remove(i)
               i = i - 1
            elseif x.modules[i-1] and
                  (x.modules[i-1].__typename == 'nn.SpatialFullConvolution') then
               absorb_bn_deconv(x.modules[i-1].weight,
                                x.modules[i-1].bias,
                                x.modules[i].running_mean,
                                x.modules[i].running_var:clone():sqrt():add(x.modules[i].eps):pow(-1),
                                x.modules[i].affine,
                                x.modules[i].weight,
                                x.modules[i].bias)
               x:remove(i)
               i = i - 1
            else
               assert(false, 'Convolution module must exist right before batch normalization layer')
            end
         elseif x.modules[i].__typename == 'nn.BatchNormalization' then
            if x.modules[i-1] and
              (x.modules[i-1].__typename == 'nn.Linear') then
               absorb_bn_conv(x.modules[i-1].weight,
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

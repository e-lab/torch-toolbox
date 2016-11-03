#include <string.h>
#include "../thnets.h"

THFloatTensor *nn_SpatialZeroPadding_updateOutput(struct module *module, THFloatTensor *input)
{
	int idim = input->nDimension;
	if(idim != 3 && idim != 4)
		THError("input dimension must be 3 or 4");
	int pad_l = module->SpatialZeroPadding.pad_l;
	int pad_r = module->SpatialZeroPadding.pad_r;
	int pad_t = module->SpatialZeroPadding.pad_t;
	int pad_b = module->SpatialZeroPadding.pad_b;
	int iw = input->size[idim-1];
	int ih = input->size[idim-2];
	int ow = iw + pad_l + pad_r;
	int oh = ih + pad_t + pad_b;
	int ix1 = pad_l < 0 ? -pad_l : 0;
	int iy1 = pad_t < 0 ? -pad_t : 0;
	int ix2 = pad_r < 0 ? iw + pad_r : iw;
	int iy2 = pad_b < 0 ? ih + pad_b : ih;
	if(idim == 3)
		THFloatTensor_resize3d(module->output, input->size[0], oh, ow);
	else THFloatTensor_resize4d(module->output, input->size[0], input->size[1], oh, ow);
	int batchsize = idim == 4 ? input->size[0] : 1;
	int batch, plane, y;
	int istride = input->size[idim-2];
	for(batch = 0; batch < batchsize; batch++)
		for(plane = 0; plane < input->size[idim - 3]; plane++)
		{
			float *in = THFloatTensor_data(input) + batch * input->stride[0] + plane * input->stride[idim-3];
			float *out = THFloatTensor_data(module->output) + batch * module->output->stride[0] + plane * module->output->stride[idim-3];
			if(pad_t > 0)
				memset(out, 0, ow * pad_t * sizeof(*out));
			if(pad_b > 0)
				memset(out + (pad_t + ih) * ow, 0, ow * pad_b * sizeof(*out));
			for(y = iy1; y < iy2; y++)
			{
				if(pad_l > 0)
					memset(out + (y + pad_t) * ow, 0, pad_l * sizeof(*out));
				if(pad_r > 0)
					memset(out + (y + pad_t) * ow + pad_l + ow, 0, pad_r * sizeof(*out));
				memcpy(out + (y + pad_t) * ow + (pad_l < 0 ? 0 : pad_l), in + y * istride + ix1, (ix2-ix1) * sizeof(*out));
			}
		}
	return module->output;
}

#include <math.h>
#include "../thnets.h"

static void nn_SpatialMaxPooling_updateOutput_frame(float *input_p, float *output_p,
	long nslices,
	long iwidth, long iheight,
	long owidth, long oheight,
	int kW, int kH, int dW, int dH,
	int padW, int padH)
{
	long k;
#pragma omp parallel for private(k)
	for (k = 0; k < nslices; k++) {
		float *ip = input_p + k*iwidth*iheight;
		float *op = output_p + k*owidth*oheight;

		long i, j;
		for (i = 0; i < oheight; i++) {
			for (j = 0; j < owidth; j++) {

				long hstart = i * dH - padH;
				long wstart = j * dW - padW;
				long hend = fminf(hstart + kH, iheight);
				long wend = fminf(wstart + kW, iwidth);
				hstart = fmaxf(hstart, 0);
				wstart = fmaxf(wstart, 0);

				float maxval = -THInf;

				long x, y;
				for (y = hstart; y < hend; y++) {
					for (x = wstart; x < wend; x++) {
						float val = *(ip + y*iwidth + x);

						if (val > maxval)
							maxval = val;
					}
				}
				*(op + i*owidth + j) = maxval;
			}
		}
	}
}

THFloatTensor *nn_SpatialMaxPooling_updateOutput(struct module *module, THFloatTensor *input)
{
	int kW = module->SpatialMaxPooling.kW;
	int kH = module->SpatialMaxPooling.kH;
	int dW = module->SpatialMaxPooling.dW;
	int dH = module->SpatialMaxPooling.dH;
	int padW = module->SpatialMaxPooling.padW;
	int padH = module->SpatialMaxPooling.padH;
	int ceil_mode = module->SpatialMaxPooling.ceil_mode;
	THFloatTensor *output = module->output;

	int batch = 1;
	if (input->nDimension == 3) {
		batch = 0;
		THFloatTensor_resize4d(input, 1, input->size[0], input->size[1], input->size[2]);
	}

	long batchSize = input->size[0];
	long nslices = input->size[1];
	long iheight = input->size[2];
	long iwidth = input->size[3];

	long oheight;
	long owidth;
	if (ceil_mode) {
		oheight = (long)(ceil((float)(iheight - kH + 2*padH) / dH)) + 1;
		owidth  = (long)(ceil((float)(iwidth  - kW + 2*padW) / dW)) + 1;
	} else {
		oheight = (long)(floor((float)(iheight - kH + 2*padH) / dH)) + 1;
		owidth  = (long)(floor((float)(iwidth  - kW + 2*padW) / dW)) + 1;
	}

	if (padW || padH) {
		// ensure that the last pooling starts inside the image
		if ((oheight - 1)*dH >= iheight + padH)
			--oheight;
		if ((owidth  - 1)*dW >= iwidth  + padW)
			--owidth;
	}

	THFloatTensor_resize4d(output, batchSize, nslices, oheight, owidth);

	float *input_data = THFloatTensor_data(input);
	float *output_data = THFloatTensor_data(output);

	long p;
#pragma omp parallel for private(p)
	for (p = 0; p < batchSize; p++) {
		nn_SpatialMaxPooling_updateOutput_frame(input_data+p*nslices*iwidth*iheight,
			output_data+p*nslices*owidth*oheight,
			nslices, iwidth, iheight, owidth, oheight,
			kW, kH, dW, dH, padW, padH);
	}

	if (batch == 0) {
		THFloatTensor_resize3d(output, nslices, oheight, owidth);
		THFloatTensor_resize3d(input, nslices, iheight, iwidth);
	}

	return output;
}

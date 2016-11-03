#include "../thnets.h"

THFloatTensor *nn_Reshape_updateOutput(struct module *module, THFloatTensor *input)
{
	long numElements = module->Reshape.numElements;
	long nElements = THFloatTensor_nElement(input);
	THFloatTensor_set(module->output, input);
	if(module->Reshape.batchMode == 0 ||
		(module->Reshape.batchMode == -1 && nElements == numElements && input->size[0] != 1))
		THFloatTensor_resize(module->output, module->Reshape.size, module->Reshape.nsize);
	else {
		module->Reshape.batchsize[0] = input->size[0];
		THFloatTensor_resize(module->output, module->Reshape.batchsize, module->Reshape.nbatchsize);
	}
	return module->output;
}

#include "../thnets.h"

THFloatTensor *nn_View_updateOutput(struct module *module, THFloatTensor *input)
{
	long nElements = THFloatTensor_nElement(input);
	long numElements = module->View.numElements;
	long batchSize = nElements / numElements;
	THFloatTensor *view;

	if (batchSize > 1)
		view = THFloatTensor_newWithStorage2d(input->storage, input->storageOffset, batchSize, numElements, numElements, 1);
	else
		view = THFloatTensor_newWithStorage1d(input->storage, input->storageOffset, numElements, 1);

	THFloatTensor_free(module->output);
	module->output = view;

	return module->output;
}

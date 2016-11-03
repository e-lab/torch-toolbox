#include "../thnets.h"

THFloatTensor *nn_Dropout_updateOutput(struct module *module, THFloatTensor *input)
{
	if(module->Dropout.inplace == 1)
		THFloatTensor_set(module->output, input);
	else {
		THFloatTensor_resizeAs(module->output, input);
		THFloatTensor_copy(module->output, input);
	}
	if(!module->Dropout.v2)
	{
		long i, n = THFloatTensor_nElement(input);
		for(i = 0; i < n; i++)
			module->output->storage->data[i] = module->output->storage->data[i] * (1 - module->Dropout.v2);
	}
	return module->output;
}

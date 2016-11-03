#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "thnets.h"

static int lasterror, longsize = 8;

THFloatTensor *forward(struct network *net, THFloatTensor *in)
{
	int i;
	
	for(i = 0; i < net->nelem; i++)
	{
		in = net->modules[i].updateOutput(&net->modules[i], in);
		//printf("%d) %d %d %ld %ld %ld %ld %f\n", i+1, net->modules[i].type, in->nDimension, in->size[0], in->size[1], in->size[2], in->size[3], in->storage->data[0]);
	}
	return in;
}

THNETWORK *THLoadNetwork(const char *path)
{
	char tmppath[255];
	int i;
	
	THNETWORK *net = malloc(sizeof(*net));
	sprintf(tmppath, "%s/model.net", path);
	lasterror = loadtorch(tmppath, &net->netobj, longsize);
	if(lasterror)
	{
		free(net);
		return 0;
	}
	//printobject(&net->netobj, 0);
	net->net = Object2Network(&net->netobj);
	if(!net->net)
	{
		lasterror = ERR_WRONGOBJECT;
		freeobject(&net->netobj);
		free(net);
		return 0;
	}
	sprintf(tmppath, "%s/stat.t7", path);
	lasterror = loadtorch(tmppath, &net->statobj, longsize);
	if(lasterror)
	{
		freenetwork(net->net);
		freeobject(&net->netobj);
		free(net);
		return 0;
	}
	if(net->statobj.type != TYPE_TABLE || net->statobj.table->nelem != 2)
	{
		lasterror = ERR_WRONGOBJECT;
		freenetwork(net->net);
		freeobject(&net->netobj);
		freeobject(&net->statobj);
		free(net);
	}
	net->std = net->mean = 0;
	for(i = 0; i < net->statobj.table->nelem; i++)
		if(net->statobj.table->records[i].name.type == TYPE_STRING)
		{
			if(!strcmp(net->statobj.table->records[i].name.string.data, "mean"))
				net->mean = net->statobj.table->records[i].value.tensor->storage->data;
			else if(!strcmp(net->statobj.table->records[i].name.string.data, "std"))
				net->std = net->statobj.table->records[i].value.tensor->storage->data;
		}
	if(!net->mean || !net->std)
	{
		lasterror = ERR_WRONGOBJECT;
		freenetwork(net->net);
		freeobject(&net->netobj);
		freeobject(&net->statobj);
		free(net);
	}
	return net;
}

int THProcessFloat(THNETWORK *network, float *data, int batchsize, int width, int height, float **result, int *outwidth, int *outheight)
{
	int b, c, i;
	THFloatTensor *t = THFloatTensor_new();
	t->nDimension = 4;
	t->size[0] = batchsize;
	t->size[1] = 3;
	t->size[2] = height;
	t->size[3] = width;
	t->stride[0] = 3 * width * height;
	t->stride[1] = width * height;
	t->stride[2] = width;
	t->stride[3] = 1;
	t->storage = THFloatStorage_newwithbuffer((float *)data);
	for(b = 0; b < batchsize; b++)
		for(c = 0; c < 3; c++)
			for(i = 0; i < width*height; i++)
				data[b * t->stride[0] + c * t->stride[1] + i] =
					(data[b * t->stride[0] + c * t->stride[1] + i] - network->mean[c]) / network->std[c];
	THFloatTensor *out = forward(network->net, t);
	THFloatTensor_free(t);
	*result = out->storage->data;
	if(out->nDimension >= 3)
	{
		*outwidth = out->size[out->nDimension - 1];
		*outheight = out->size[out->nDimension - 2];
	} else *outwidth = *outheight = 1;
	return THFloatTensor_nElement(out);
}

#define BYTE2FLOAT 0.003921568f // 1/255

void rgb2float(float *dst, const unsigned char *src, int width, int height, int srcstride, const float *mean, const float *std)
{
	int c, i, j;

	for(c = 0; c < 3; c++)
		for(i = 0; i < height; i++)
			for(j = 0; j < width; j++)
				dst[j + (i + c * height) * width] = (src[c + 3*j + srcstride*i] * BYTE2FLOAT - mean[c]) / std[c];
}

int THProcessImages(THNETWORK *network, unsigned char **images, int batchsize, int width, int height, int stride, float **results, int *outwidth, int *outheight)
{
	int i;
	
	float *data = malloc(batchsize * width * height * 3 * sizeof(*data));
	for(i = 0; i < batchsize; i++)
		rgb2float(data + i * width * height * 3, images[i], width, height, stride, network->mean, network->std);
	THFloatTensor *t = THFloatTensor_new();
	t->nDimension = 4;
	t->size[0] = batchsize;
	t->size[1] = 3;
	t->size[2] = height;
	t->size[3] = width;
	t->stride[0] = 3 * width * height;
	t->stride[1] = width * height;
	t->stride[2] = width;
	t->stride[3] = 1;
	t->storage = THFloatStorage_newwithbuffer((float *)data);
	THFloatTensor *out = forward(network->net, t);
	THFloatTensor_free(t);
	free(data);
	*results = out->storage->data;
	if(out->nDimension >= 3)
	{
		*outwidth = out->size[out->nDimension - 1];
		*outheight = out->size[out->nDimension - 2];
	} else *outwidth = *outheight = 1;
	return THFloatTensor_nElement(out);
}

void THFreeNetwork(THNETWORK *network)
{
	freenetwork(network->net);
	freeobject(&network->netobj);
	freeobject(&network->statobj);
	free(network);
}

int THLastError()
{
	return lasterror;
}

void THMakeSpatial(THNETWORK *network)
{
	int i, size = 231, nInputPlane = 3;
	
	for(i = 0; i < network->net->nelem; i++)
	{
		if(network->net->modules[i].type == MT_View || network->net->modules[i].type == MT_Reshape)
		{
			THFloatTensor_free(network->net->modules[i].output);
			memmove(network->net->modules+i, network->net->modules+i+1, sizeof(*network->net->modules) * (network->net->nelem - i - 1));
			network->net->nelem--;
			i--;
		} else if(network->net->modules[i].type == MT_Linear)
		{
			THFloatTensor_free(network->net->modules[i].Linear.addBuffer);
			network->net->modules[i].type = MT_SpatialConvolutionMM;
			network->net->modules[i].updateOutput = nn_SpatialConvolutionMM_updateOutput;
			struct SpatialConvolutionMM *c = &network->net->modules[i].SpatialConvolutionMM;
			c->finput = THFloatTensor_new();
			c->padW = c->padH = 0;
			c->dW = c->dH = 1;
			c->kW = c->kH = size;
			c->nInputPlane = nInputPlane;
			nInputPlane = c->nOutputPlane = c->weight->size[0];
			size = (size + 2*c->padW - c->kW) / c->dW + 1;
		} else if(network->net->modules[i].type == MT_SpatialConvolutionMM)
		{
			struct SpatialConvolutionMM *c = &network->net->modules[i].SpatialConvolutionMM;
			size = (size + 2*c->padW - c->kW) / c->dW + 1;
			nInputPlane = network->net->modules[i].SpatialConvolutionMM.nOutputPlane;
		} else if(network->net->modules[i].type == MT_SpatialMaxPooling)
		{
			struct SpatialMaxPooling *c = &network->net->modules[i].SpatialMaxPooling;
			if(c->ceil_mode)
				size = (long)(ceil((float)(size - c->kH + 2*c->padH) / c->dH)) + 1;
			else size = (long)(floor((float)(size - c->kH + 2*c->padH) / c->dH)) + 1;
		} else if(network->net->modules[i].type == MT_SpatialZeroPadding)
		{
			struct SpatialZeroPadding *c = &network->net->modules[i].SpatialZeroPadding;
			size += c->pad_l + c->pad_r;
		}
	}
}

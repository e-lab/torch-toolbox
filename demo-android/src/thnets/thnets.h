#include <float.h>
#ifdef MEMORYDEBUG
#include "memory.h"
#endif

enum therror {
	ERR_OPENFILE = -1,
	ERR_READFILE = -2,
	ERR_NOTIMPLEMENTED = -3,
	ERR_CORRUPTED = -4,
	ERR_WRONGOBJECT = -5
};

enum thtype {
   TYPE_NIL      = 0,
   TYPE_NUMBER   = 1,
   TYPE_STRING   = 2,
   TYPE_TABLE    = 3,
   TYPE_TORCH    = 4,
   TYPE_BOOLEAN  = 5,
   TYPE_FUNCTION = 6,
   LEGACY_TYPE_RECUR_FUNCTION = 7,
   TYPE_RECUR_FUNCTION = 8,
   TYPE_BYTE     = 100,
   TYPE_CHAR     = 101,
   TYPE_SHORT    = 102,
   TYPE_INT      = 103,
   TYPE_LONG     = 104,
   TYPE_FLOAT    = 105,
   TYPE_DOUBLE   = 106,
   TYPE_STORAGE  = 200,
   TYPE_TENSOR   = 201,
   TYPE_NNMODULE  =202
};

struct thobject;
struct threcord;

struct table {
	int idx;
	int nrefs;
	int nelem;
	struct threcord *records;
};

struct nnmodule {
	int idx;
	int nrefs;
	char *name;
	struct table *table;
};

struct storage {
	int idx;
	int nrefs;
	int scalartype;
	long nelem;
	void *data;
};

struct tensor {
	int idx;
	int nrefs;
	int scalartype;
	int ndim;
	long *size;
	long *stride;
	long storageoffset;
	struct storage *storage;
} tensor;

struct thobject
{
	int type;
	union {
		double number;
		struct {
			int size;
			char *data;
		} string;
		struct table *table;
		struct storage *storage;
		struct tensor *tensor;
		struct nnmodule *nnmodule;
		int boolean;
	};
};

struct threcord {
	struct thobject name;
	struct thobject value;
};

typedef struct THFloatStorage
{
    float *data;
	int nref, mustfree;
} THFloatStorage;

typedef struct THFloatTensor
{
    long size[4];
    long stride[4];
    int nDimension;    
	THFloatStorage *storage;
	long storageOffset;
} THFloatTensor;

struct SpatialConvolutionMM
{
	THFloatTensor *bias, *weight, *finput;
	int padW, padH, dW, dH, kW, kH, nInputPlane, nOutputPlane;
};

struct SpatialMaxPooling
{
	int padW, padH, dW, dH, kW, kH, ceil_mode;
};

struct Linear
{
	THFloatTensor *bias, *weight, *addBuffer;
};

struct Threshold
{
	float threshold, val;
	int inplace;
};

struct View
{
	int size, numElements;
};

struct Dropout
{
	float p;
	int inplace, v2;
};

struct SpatialZeroPadding
{
	int pad_l, pad_r, pad_t, pad_b;
};

struct Reshape
{
	int numElements, batchMode;
	long size[4], batchsize[4];
	int nsize, nbatchsize;
};

enum moduletype {
	MT_Nil,
	MT_SpatialConvolutionMM,
	MT_SpatialMaxPooling,
	MT_Linear,
	MT_SoftMax,
	MT_Threshold,
	MT_View,
	MT_Dropout,
	MT_SpatialZeroPadding,
	MT_Reshape
};

struct module
{
	int type;
	THFloatTensor *(*updateOutput)(struct module *m, THFloatTensor *in);
	THFloatTensor *output;
	union {
		struct SpatialConvolutionMM SpatialConvolutionMM;
		struct SpatialMaxPooling SpatialMaxPooling;
		struct Linear Linear;
		struct Threshold Threshold;
		struct View View;
		struct Dropout Dropout;
		struct SpatialZeroPadding SpatialZeroPadding;
		struct Reshape Reshape;
	};
};

struct network
{
	int nelem;
	struct module *modules;
};

void THError(const char *fmt, ...);
THFloatTensor *THFloatTensor_new();
THFloatStorage *THFloatStorage_newwithbuffer(void *buffer);
THFloatTensor *THFloatTensor_newWithStorage1d(THFloatStorage *storage, long storageOffset, long size0, long stride0);
THFloatTensor *THFloatTensor_newWithStorage2d(THFloatStorage *storage, long storageOffset, long size0, long stride0, long size1, long stride1);
THFloatTensor *THFloatTensor_newWithTensor(THFloatTensor *tensor);
void THFloatTensor_transpose(THFloatTensor *tdst, THFloatTensor *tsrc, int dimension1, int dimension2);
THFloatTensor *THFloatTensor_newTranspose(THFloatTensor *tensor, int dimension1_, int dimension2_);
float *THFloatTensor_data(THFloatTensor *tensor);
void THFloatTensor_resize(THFloatTensor *t, long *size, int nDimension);
void THFloatTensor_resize4d(THFloatTensor *t, long size0, long size1, long size2, long size3);
void THFloatTensor_resize3d(THFloatTensor *t, long size0, long size1, long size2);
void THFloatTensor_resize2d(THFloatTensor *t, long size0, long size1);
void THFloatTensor_resize1d(THFloatTensor *t, long size0);
void THFloatTensor_resizeAs(THFloatTensor *tdst, THFloatTensor *tsrc);
long THFloatTensor_nElement(THFloatTensor *t);
void THFloatTensor_set(THFloatTensor *tdst, THFloatTensor *tsrc);
void THFloatTensor_zero(THFloatTensor *t);
void THFloatTensor_fill(THFloatTensor *t, float value);
void THFloatTensor_copy(THFloatTensor *tdst, THFloatTensor *tsrc);
void THFloatTensor_free(THFloatTensor *t);
THFloatTensor *THFloatTensor_newSelect(THFloatTensor *tensor, int dimension, long sliceIndex);
float *THFloatTensor_data(THFloatTensor *tensor);
void THFloatVector_fill(float *x, float c, long n);
double THExpMinusApprox(double x);
void THFloatTensor_addmm(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *m1, THFloatTensor *m2);
void THFloatTensor_addr(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *vec1, THFloatTensor *vec2);
void THFloatTensor_addmv(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *mat, THFloatTensor *vec);

#define fmaxf(a,b) ((a) > (b) ? (a) : (b))
#define fminf(a,b) ((a) < (b) ? (a) : (b))
#define THInf FLT_MAX

int loadtorch(const char *path, struct thobject *obj, int longsize);
int printobject(struct thobject *obj, int indent);
int freeobject(struct thobject *obj);
void freenetwork(struct network *net);
THFloatTensor *forward(struct network *net, THFloatTensor *in);
THFloatTensor *THFloatTensor_newFromObject(struct thobject *obj);
struct network *Object2Network(struct thobject *obj);

THFloatTensor *nn_SpatialConvolutionMM_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_SpatialMaxPooling_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_Threshold_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_View_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_SoftMax_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_Linear_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_Dropout_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_SpatialZeroPadding_updateOutput(struct module *module, THFloatTensor *input);
THFloatTensor *nn_Reshape_updateOutput(struct module *module, THFloatTensor *input);

/* High level API */

typedef struct thnetwork
{
	struct thobject netobj;
	struct thobject statobj;
	struct network *net;
	float *mean, *std;
} THNETWORK;

THNETWORK *THLoadNetwork(const char *path);
void THMakeSpatial(THNETWORK *network);
int THProcessFloat(THNETWORK *network, float *data, int batchsize, int width, int height, float **result, int *outwidth, int *outheight);
int THProcessImages(THNETWORK *network, unsigned char **images, int batchsize, int width, int height, int stride, float **result, int *outwidth, int *outheight);
void THFreeNetwork(THNETWORK *network);
int THLastError();

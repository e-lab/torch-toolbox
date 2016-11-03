#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "thnets.h"

struct thfile
{
	FILE *fp;
	int idx;
	int longsize;
	int refalloc;
	struct thobject **refobjects;
};

static int readobject(struct thfile *f, struct thobject *obj);
int freeobject(struct thobject *obj);

static int readint(struct thfile *f, int *v)
{
	return fread(v, sizeof(*v), 1, f->fp) == 1 ? 0 : ERR_READFILE;
}

static int readlong(struct thfile *f, long *v)
{
	if(f->longsize == 0 || f->longsize == sizeof(long))
		return fread(v, sizeof(*v), 1, f->fp) == 1 ? 0 : ERR_READFILE;
	else if(f->longsize == 4)
	{
		long l;
		if(fread(&l, 4, 1, f->fp) != 1)
			return ERR_READFILE;
		*v = l;
		return 0;
	} else {
		long l[2];
		if(fread(l, 4, 2, f->fp) != 2)
			return ERR_READFILE;
		*v = l[0];
		return 0;
	}
}

static int readdouble(struct thfile *f, double *v)
{
	return fread(v, sizeof(*v), 1, f->fp) == 1 ? 0 : ERR_READFILE;
}

static int readstring(struct thfile *f, char **s, int *size)
{
	if(readint(f, size))
		return ERR_READFILE;
	*s = malloc(*size+1);
	if(fread(*s, *size, 1, f->fp) != 1)
	{
		free(*s);
		*s = 0;
		return ERR_READFILE;
	}
	(*s)[*size] = 0;
	return 0;
}

static int scalartype(const char *type)
{
	if(!strcmp(type, "Byte"))
		return TYPE_BYTE;
	if(!strcmp(type, "Char"))
		return TYPE_CHAR;
	if(!strcmp(type, "Short"))
		return TYPE_SHORT;
	if(!strcmp(type, "Int"))
		return TYPE_INT;
	if(!strcmp(type, "Long"))
		return TYPE_LONG;
	if(!strcmp(type, "Float"))
		return TYPE_FLOAT;
	if(!strcmp(type, "Double"))
		return TYPE_DOUBLE;
	return ERR_CORRUPTED;
}

static int scalarsize(int type)
{
	switch(type)
	{
	case TYPE_BYTE:
	case TYPE_CHAR:
		return 1;
	case TYPE_SHORT:
		return 2;
	case TYPE_LONG:
		return sizeof(long);
	case TYPE_INT:
	case TYPE_FLOAT:
		return 4;
	case TYPE_DOUBLE:
		return 8;
	default:
		return ERR_CORRUPTED;
	}
}

static const char *scalarname(int type)
{
	static const char *names[] = {"Byte", "Char", "Short", "Int", "Long", "Float", "Double"};
	
	if(type >= TYPE_CHAR && type <= TYPE_DOUBLE)
		return names[type - TYPE_BYTE];
	return "Unknown";
}

static struct thobject *findidx(struct thfile *f, int idx)
{
	if(idx < 0 || idx >= f->idx)
		return 0;
	return f->refobjects[idx];
}

static int readtable(struct thfile *f, struct thobject *obj)
{
	int idx, rc, i;
	
	if(readint(f, &idx))
		return ERR_READFILE;
	if(idx != f->idx)
	{
		struct thobject *o = findidx(f, idx);
		if(!o)
			return ERR_CORRUPTED;
		if(o->type == TYPE_TABLE)
		{
			obj->table = o->table;
			obj->table->nrefs++;
			return 0;
		}
		if(o->type == TYPE_NNMODULE && o->nnmodule->table && o->nnmodule->table->idx == idx)
		{
			obj->table = o->nnmodule->table;
			obj->table->nrefs++;
			return 0;
		}
		return ERR_CORRUPTED;
	}
	if(f->idx == f->refalloc)
	{
		f->refalloc *= 2;
		f->refobjects = realloc(f->refobjects, f->refalloc * sizeof(*f->refobjects));
	}
	f->refobjects[f->idx] = obj;
	f->idx++;
	obj->table = calloc(1, sizeof(*obj->table));
	obj->table->idx = idx;
	obj->table->nrefs = 1;
	if(readint(f, &obj->table->nelem))
		return ERR_READFILE;
	obj->table->records = calloc(obj->table->nelem, sizeof(obj->table->records[0]));
	for(i = 0; i < obj->table->nelem; i++)
	{
		rc = readobject(f, &obj->table->records[i].name);
		if(rc)
			return rc;
		rc = readobject(f, &obj->table->records[i].value);
		if(rc)
			return rc;
	}
	return 0;
}

static int readtorchstorage(const char *type, struct thfile *f, struct thobject *obj, int idx)
{
	int ss;

	obj->type = TYPE_STORAGE;
	obj->storage = calloc(1, sizeof(*obj->storage));
	obj->storage->idx = idx;
	obj->storage->nrefs = 1;
	obj->storage->scalartype = scalartype(type);
	ss = scalarsize(obj->storage->scalartype);
	if(ss < 0)
		return ss;
	if(readlong(f, &obj->storage->nelem))
		return ERR_READFILE;
	obj->storage->data = malloc(obj->storage->nelem * ss);
	if(obj->storage->scalartype == TYPE_LONG && f->longsize > 0 && f->longsize == 4 && sizeof(long) == 8)
	{
		long i;
		int *tmp = malloc(obj->storage->nelem * 4);
		if(fread(tmp, 4, obj->storage->nelem, f->fp) != obj->storage->nelem)
			return ERR_READFILE;
		for(i = 0; i < obj->storage->nelem; i++)
			((long*)obj->storage->data)[i] = tmp[i];
		free(tmp);
	} else if(obj->storage->scalartype == TYPE_LONG && f->longsize > 0 && f->longsize == 8 && sizeof(long) == 4)
	{
		long i;
		long *tmp = malloc(obj->storage->nelem * 8);
		if(fread(tmp, 8, obj->storage->nelem, f->fp) != obj->storage->nelem)
			return ERR_READFILE;
		for(i = 0; i < obj->storage->nelem; i++)
			((long*)obj->storage->data)[i] = tmp[2*i];
		free(tmp);
	} else if(fread(obj->storage->data, ss, obj->storage->nelem, f->fp) != obj->storage->nelem)
		return ERR_READFILE;
	return 0;
}

static int readtorchtensor(const char *type, struct thfile *f, struct thobject *obj, int idx)
{
	int i, rc;
	struct thobject tmp;

	obj->type = TYPE_TENSOR;
	obj->tensor = calloc(1, sizeof(*obj->tensor));
	obj->tensor->idx = idx;
	obj->tensor->nrefs = 1;
	obj->tensor->scalartype = scalartype(type);
	if(readint(f, &obj->tensor->ndim))
		return ERR_READFILE;
	obj->tensor->size = malloc(obj->tensor->ndim * sizeof(*obj->tensor->size));
	obj->tensor->stride = malloc(obj->tensor->ndim * sizeof(*obj->tensor->stride));
	for(i = 0; i < obj->tensor->ndim; i++)
		if(readlong(f, &obj->tensor->size[i]))
			return ERR_READFILE;
	for(i = 0; i < obj->tensor->ndim; i++)
		if(readlong(f, &obj->tensor->stride[i]))
			return ERR_READFILE;
	if(readlong(f, &obj->tensor->storageoffset))
		return ERR_READFILE;
	obj->tensor->storageoffset--;
	int curidx = f->idx;
	rc = readobject(f, &tmp);
	if(rc)
	{
		freeobject(&tmp);
		return rc;
	}
	if(tmp.type != TYPE_STORAGE && tmp.type != TYPE_NIL)
	{
		freeobject(&tmp);
		return ERR_CORRUPTED;
	}
	obj->tensor->storage = tmp.storage;
	if(tmp.storage)
		f->refobjects[curidx] = obj;
	return 0;
}

static int readtorch(struct thfile *f, struct thobject *obj)
{
	int idx, rc;
	char *s;
	int size;
	
	if(readint(f, &idx))
		return ERR_READFILE;
	if(idx != f->idx)
	{
		struct thobject *o = findidx(f, idx);
		if(!o)
			return ERR_CORRUPTED;
		if(o->type == TYPE_TENSOR && o->tensor->idx == idx)
		{
			obj->type = TYPE_TENSOR;
			obj->tensor = o->tensor;
			obj->tensor->nrefs++;
			return 0;
		}
		if(o->type == TYPE_TENSOR && o->tensor->storage && o->tensor->storage->idx == idx)
		{
			obj->type = TYPE_STORAGE;
			obj->storage = o->tensor->storage;
			obj->storage->nrefs++;
			return 0;
		}
		if(o->type == TYPE_STORAGE && o->storage->idx == idx)
		{
			obj->type = TYPE_STORAGE;
			obj->storage = o->storage;
			obj->storage->nrefs++;
			return 0;
		}
		if(o->type == TYPE_NNMODULE && o->nnmodule->idx == idx)
		{
			obj->type = TYPE_NNMODULE;
			obj->nnmodule = o->nnmodule;
			obj->nnmodule->nrefs++;
			return 0;
		}
		return ERR_CORRUPTED;
	}
	if(f->idx == f->refalloc)
	{
		f->refalloc *= 2;
		f->refobjects = realloc(f->refobjects, f->refalloc * sizeof(*f->refobjects));
	}
	f->refobjects[f->idx] = obj;
	f->idx++;
	if(readstring(f, &s, &size))
		return ERR_READFILE;
	free(s);
	if(readstring(f, &s, &size))
		return ERR_READFILE;
	if(!memcmp(s, "torch.", 6) && !strcmp(s + strlen(s) - 6, "Tensor"))
	{
		s[strlen(s) - 6] = 0;
		rc = readtorchtensor(s+6, f, obj, idx);
		free(s);
		return rc;
	} else if(!memcmp(s, "torch.", 6) && !strcmp(s + strlen(s) - 7, "Storage"))
	{
		s[strlen(s) - 7] = 0;
		rc = readtorchstorage(s+6, f, obj, idx);
		free(s);
		return rc;
	} else if(!memcmp(s, "nn.", 3))
	{
		struct thobject tmp;
		
		obj->type = TYPE_NNMODULE;
		obj->nnmodule = calloc(1, sizeof(*obj->nnmodule));
		obj->nnmodule->idx = idx;
		obj->nnmodule->nrefs = 1;
		obj->nnmodule->name = s;
		int curidx = f->idx;
		rc = readobject(f, &tmp);
		if(rc)
		{
			freeobject(&tmp);
			return rc;
		}
		if(tmp.type != TYPE_TABLE)
		{
			freeobject(&tmp);
			return ERR_CORRUPTED;
		}
		f->refobjects[curidx] = obj;
		obj->nnmodule->table = tmp.table;
		return 0;
	} else return ERR_NOTIMPLEMENTED;
}
	
static int readobject(struct thfile *f, struct thobject *obj)
{
	int rc;

	memset(obj, 0, sizeof(*obj));
	if(readint(f, &obj->type))
		return ERR_READFILE;
	switch(obj->type)
	{
	case TYPE_NIL:
		break;
	case TYPE_NUMBER:
		if(readdouble(f, &obj->number))
			return ERR_READFILE;
		break;	
	case TYPE_STRING:
		if(readstring(f, &obj->string.data, &obj->string.size))
			return ERR_READFILE;
		break;
	case TYPE_TABLE:
		rc = readtable(f, obj);
		if(rc)
			return rc;
		break;
	case TYPE_TORCH:
		rc = readtorch(f, obj);
		if(rc)
			return rc;
		break;
	case TYPE_BOOLEAN:
		if(readint(f, &obj->boolean))
			return ERR_READFILE;
		break;	
	case TYPE_FUNCTION:
	case LEGACY_TYPE_RECUR_FUNCTION:
	case TYPE_RECUR_FUNCTION:
		return ERR_NOTIMPLEMENTED;
	default:
		return ERR_CORRUPTED;
	}
	return 0;
}

int loadtorch(const char *path, struct thobject *obj, int longsize)
{
	FILE *fp = fopen(path, "rb");
	if(!fp)
		return ERR_OPENFILE;
	struct thfile *f = malloc(sizeof(*f));
	f->fp = fp;
	f->idx = 1;
	f->longsize = longsize;
	f->refalloc = 1000;
	f->refobjects = malloc(f->refalloc * sizeof(*f->refobjects));
	int rc = readobject(f, obj);
	fclose(fp);
	free(f->refobjects);
	free(f);
	if(rc)
		freeobject(obj);
	return rc;
}

static void printindent(int indent)
{
	int i;

	for(i = 0; i < indent; i++)
		printf("    ");
}

int printobject(struct thobject *obj, int indent)
{
	int i;

	printindent(indent);
	switch(obj->type)
	{
	case TYPE_NUMBER:
		printf("%f\n", obj->number);
		break;
	case TYPE_STRING:
		printf("\"%*.*s\"\n", obj->string.size, obj->string.size, obj->string.data);
		break;
	case TYPE_TABLE:
		printf("Table long %d\n", obj->table->nelem);
		for(i = 0; i < obj->table->nelem; i++)
		{
			printindent(indent);
			printf("%d/%d: ", i+1, obj->table->nelem);
			printobject(&obj->table->records[i].name, 0);
			printobject(&obj->table->records[i].value, indent+1);
		}
		break;
	case TYPE_BOOLEAN:
		printf("%s\n", obj->boolean ? "true" : "false");
		break;
	case TYPE_STORAGE:
		printf("%s storage of %ld elements\n", scalarname(obj->storage->scalartype), obj->storage->nelem);
		break;
	case TYPE_TENSOR:
		if(obj->tensor->ndim == 0)
			printf("%s tensor of dimension 0\n", scalarname(obj->tensor->scalartype));
		else {
			printf("%s tensor of dimension %d (", scalarname(obj->tensor->scalartype), obj->tensor->ndim);
			for(i = 0; i < obj->tensor->ndim; i++)
				printf("%ld%s", obj->tensor->size[i], i == obj->tensor->ndim - 1 ? ")\n" : ",");
		}
		break;
	case TYPE_NNMODULE:
		if(!obj->nnmodule->table)
		{
			printf("nn module %s (not loaded)\n", obj->nnmodule->name);
			break;
		}
		printf("nn module %s with %d elements\n", obj->nnmodule->name, obj->nnmodule->table->nelem);
		for(i = 0; i < obj->nnmodule->table->nelem; i++)
		{
			printindent(indent);
			printf("%d/%d: ", i+1, obj->nnmodule->table->nelem);
			printobject(&obj->nnmodule->table->records[i].name, 0);
			printobject(&obj->nnmodule->table->records[i].value, indent+1);
		}
		break;
	}
	return 0;
}

int freeobject(struct thobject *obj)
{
	int i;

	switch(obj->type)
	{
	case TYPE_STRING:
		if(obj->string.data)
		{
			free(obj->string.data);
			obj->string.data = 0;
		}
		break;
	case TYPE_TABLE:
		if(obj->table)
		{
			if(obj->table->nrefs > 1)
				obj->table->nrefs--;
			else {
				if(obj->table->records)
				{
					for(i = 0; i < obj->table->nelem; i++)
					{
						freeobject(&obj->table->records[i].name);
						freeobject(&obj->table->records[i].value);
					}
					free(obj->table->records);
					obj->table->records = 0;
				}
				obj->table->nrefs--;
				free(obj->table);
				obj->table = 0;
			}
		}
		break;
	case TYPE_STORAGE:
		if(obj->storage)
		{
			if(obj->storage->nrefs > 1)
				obj->storage->nrefs--;
			else {
				if(obj->storage->data)
				{
					free(obj->storage->data);
					obj->storage->data = 0;
				}
				obj->storage->nrefs--;
				free(obj->storage);
				obj->storage = 0;
			}
		}
		break;
	case TYPE_TENSOR:
		if(obj->tensor)
		{
			if(obj->tensor->nrefs > 1)
				obj->tensor->nrefs--;
			else {
				if(obj->tensor->size)
				{
					free(obj->tensor->size);
					obj->tensor->size = 0;
				}
				if(obj->tensor->stride)
				{
					free(obj->tensor->stride);
					obj->tensor->stride = 0;
				}
				if(obj->tensor->storage)
				{
					if(obj->tensor->storage->data)
					{
						free(obj->tensor->storage->data);
						obj->tensor->storage->data = 0;
					}
					if(obj->tensor->storage->nrefs > 1)
						obj->tensor->storage->nrefs--;
					else {
						obj->tensor->storage->nrefs--;
						free(obj->tensor->storage);
						obj->tensor->storage = 0;
					}
				}
				obj->tensor->nrefs--;
				free(obj->tensor);
				obj->tensor = 0;
			}
		}
		break;
	case TYPE_NNMODULE:
		if(obj->nnmodule)
		{
			if(obj->nnmodule->nrefs > 1)
				obj->nnmodule->nrefs--;
			else {
				if(obj->nnmodule->name)
				{
					free(obj->nnmodule->name);
					obj->nnmodule->name = 0;
				}
				if(obj->nnmodule->table)
				{
					if(obj->nnmodule->table->nrefs > 1)
						obj->nnmodule->table->nrefs--;
					else {
						if(obj->nnmodule->table->records)
						{
							for(i = 0; i < obj->nnmodule->table->nelem; i++)
							{
								freeobject(&obj->nnmodule->table->records[i].name);
								freeobject(&obj->nnmodule->table->records[i].value);
							}
							free(obj->nnmodule->table->records);
							obj->nnmodule->table->records = 0;
						}
						obj->nnmodule->table->nrefs--;
						free(obj->nnmodule->table);
						obj->nnmodule->table = 0;
					}
				}
				obj->nnmodule->nrefs--;
				free(obj->nnmodule);
				obj->nnmodule = 0;
			}
		}
		break;
	}
	return 0;	
}

static double TableGetNumber(struct table *t, const char *name)
{
	int i;
	
	for(i = 0; i < t->nelem; i++)
		if(t->records[i].name.type == TYPE_STRING && !strcmp(t->records[i].name.string.data, name))
			return t->records[i].value.number;
	return 0;
}

static int TableGetBoolean(struct table *t, const char *name)
{
	int i;
	
	for(i = 0; i < t->nelem; i++)
		if(t->records[i].name.type == TYPE_STRING && !strcmp(t->records[i].name.string.data, name))
			return t->records[i].value.boolean;
	return -1;
}

THFloatTensor *TableGetTensor(struct table *t, const char *name)
{
	int i;
	
	THFloatTensor *th = THFloatTensor_new();
	for(i = 0; i < t->nelem; i++)
		if(t->records[i].name.type == TYPE_STRING && !strcmp(t->records[i].name.string.data, name))
		{
			struct tensor *tt = t->records[i].value.tensor;
			th->nDimension = tt->ndim;
			memcpy(th->size, tt->size, sizeof(long) * tt->ndim);
			memcpy(th->stride, tt->stride, sizeof(long) * tt->ndim);
			th->storage = THFloatStorage_newwithbuffer(tt->storage->data);
			break;
		}
	return th;
}

void *TableGetStorage(struct table *t, const char *name, int *nelem)
{
	int i;
	
	for(i = 0; i < t->nelem; i++)
		if(t->records[i].name.type == TYPE_STRING && !strcmp(t->records[i].name.string.data, name))
		{
			struct storage *tt = t->records[i].value.storage;
			*nelem = tt->nelem;
			return tt->data;
		}
	return 0;
}

THFloatTensor *THFloatTensor_newFromObject(struct thobject *obj)
{
	THFloatTensor *th = THFloatTensor_new();

	th->nDimension = obj->tensor->ndim;
	memcpy(th->size, obj->tensor->size, sizeof(long) * obj->tensor->ndim);
	memcpy(th->stride, obj->tensor->stride, sizeof(long) * obj->tensor->ndim);
	th->storage = THFloatStorage_newwithbuffer(obj->tensor->storage->data);
	return th;
}

struct network *Object2Network(struct thobject *obj)
{
	struct network *net;
	struct table *mt;
	int i;
	
	if(obj->type != TYPE_NNMODULE)
		return 0;
	if(strcmp(obj->nnmodule->name, "nn.Sequential"))
		return 0;
	for(i = 0; i < obj->nnmodule->table->nelem; i++)
	{
		if(!strcmp(obj->nnmodule->table->records[i].name.string.data, "modules"))
			break;
	}
	if(i == obj->nnmodule->table->nelem)
		return 0;
	net = malloc(sizeof(*net));
	mt = obj->nnmodule->table->records[i].value.table;
	net->nelem = mt->nelem;
	net->modules = calloc(mt->nelem, sizeof(*net->modules));
	for(i = 0; i < mt->nelem; i++)
	{
		struct table *t = mt->records[i].value.nnmodule->table;
		net->modules[i].output = THFloatTensor_new();
		if(!strcmp(mt->records[i].value.nnmodule->name, "nn.SpatialConvolutionMM") ||
			!strcmp(mt->records[i].value.nnmodule->name, "nn.SpatialConvolution"))
		{
			net->modules[i].type = MT_SpatialConvolutionMM;
			net->modules[i].updateOutput = nn_SpatialConvolutionMM_updateOutput;
			struct SpatialConvolutionMM *m = &net->modules[i].SpatialConvolutionMM;
			m->padW = TableGetNumber(t, "padW");
			m->padH = TableGetNumber(t, "padH");
			if(!m->padW && !m->padH)
				m->padW = m->padH = TableGetNumber(t, "padding");
			m->dW = TableGetNumber(t, "dW");
			m->dH = TableGetNumber(t, "dH");
			m->kW = TableGetNumber(t, "kW");
			m->kH = TableGetNumber(t, "kH");
			m->nInputPlane = TableGetNumber(t, "nInputPlane");
			m->nOutputPlane = TableGetNumber(t, "nOutputPlane");
			m->bias = TableGetTensor(t, "bias");
			m->weight = TableGetTensor(t, "weight");
			if(!strcmp(mt->records[i].value.nnmodule->name, "nn.SpatialConvolution"))
				THFloatTensor_resize2d(m->weight, m->weight->size[0], m->weight->size[1] * m->weight->size[2] * m->weight->size[3]);
			m->finput = THFloatTensor_new();
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.SpatialMaxPooling"))
		{
			net->modules[i].type = MT_SpatialMaxPooling;
			net->modules[i].updateOutput = nn_SpatialMaxPooling_updateOutput;
			struct SpatialMaxPooling *m = &net->modules[i].SpatialMaxPooling;
			m->padW = TableGetNumber(t, "padW");
			m->padH = TableGetNumber(t, "padH");
			m->dW = TableGetNumber(t, "dW");
			m->dH = TableGetNumber(t, "dH");
			m->kW = TableGetNumber(t, "kW");
			m->kH = TableGetNumber(t, "kH");
			m->ceil_mode = TableGetNumber(t, "ceil_mode");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.Linear"))
		{
			net->modules[i].type = MT_Linear;
			net->modules[i].updateOutput = nn_Linear_updateOutput;
			struct Linear *m = &net->modules[i].Linear;
			m->weight = TableGetTensor(t, "weight");
			m->addBuffer = TableGetTensor(t, "addBuffer");
			m->bias = TableGetTensor(t, "bias");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.SoftMax"))
		{
			net->modules[i].type = MT_SoftMax;
			net->modules[i].updateOutput = nn_SoftMax_updateOutput;
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.Threshold") ||
			!strcmp(mt->records[i].value.nnmodule->name, "nn.ReLU"))
		{
			net->modules[i].type = MT_Threshold;
			net->modules[i].updateOutput = nn_Threshold_updateOutput;
			struct Threshold *m = &net->modules[i].Threshold;
			m->threshold = TableGetNumber(t, "threshold");
			m->val = TableGetNumber(t, "val");
			m->inplace = TableGetBoolean(t, "inplace");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.View"))
		{
			net->modules[i].type = MT_View;
			net->modules[i].updateOutput = nn_View_updateOutput;
			struct View *m = &net->modules[i].View;
			m->numElements = TableGetNumber(t, "numElements");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.Dropout"))
		{
			net->modules[i].type = MT_Dropout;
			net->modules[i].updateOutput = nn_Dropout_updateOutput;
			struct Dropout *m = &net->modules[i].Dropout;
			m->inplace = TableGetBoolean(t, "inplace");
			m->v2 = TableGetBoolean(t, "v2");
			m->p = TableGetNumber(t, "p");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.SpatialZeroPadding"))
		{
			net->modules[i].type = MT_SpatialZeroPadding;
			net->modules[i].updateOutput = nn_SpatialZeroPadding_updateOutput;
			struct SpatialZeroPadding *m = &net->modules[i].SpatialZeroPadding;
			m->pad_l = TableGetNumber(t, "pad_l");
			m->pad_r = TableGetNumber(t, "pad_r");
			m->pad_t = TableGetNumber(t, "pad_t");
			m->pad_b = TableGetNumber(t, "pad_b");
		} else if(!strcmp(mt->records[i].value.nnmodule->name, "nn.Reshape"))
		{
			net->modules[i].type = MT_Reshape;
			net->modules[i].updateOutput = nn_Reshape_updateOutput;
			struct Reshape *m = &net->modules[i].Reshape;
			m->numElements = TableGetNumber(t, "nelement");
			m->batchMode = TableGetBoolean(t, "batchMode");
			void *data = TableGetStorage(t, "size", &m->nsize);
			if(data && m->nsize <= 4)
				memcpy(m->size, data, sizeof(*m->size) * m->nsize);
			data = TableGetStorage(t, "batchsize", &m->nbatchsize);
			if(data && m->nbatchsize <= 4)
				memcpy(m->batchsize, data, sizeof(*m->batchsize) * m->nbatchsize);
			
		} else THError("Unknown module type %s", mt->records[i].value.nnmodule->name);
	}
	return net;
}

void freenetwork(struct network *net)
{
	int i;

	for(i = 0; i < net->nelem; i++)
	{
		switch(net->modules[i].type)
		{
		case MT_SpatialConvolutionMM:
			THFloatTensor_free(net->modules[i].SpatialConvolutionMM.bias);
			THFloatTensor_free(net->modules[i].SpatialConvolutionMM.weight);
			THFloatTensor_free(net->modules[i].SpatialConvolutionMM.finput);
			break;
		case MT_Linear:
			THFloatTensor_free(net->modules[i].Linear.bias);
			THFloatTensor_free(net->modules[i].Linear.weight);
			THFloatTensor_free(net->modules[i].Linear.addBuffer);
		}
		THFloatTensor_free(net->modules[i].output);
	}
	free(net->modules);
	free(net);
}

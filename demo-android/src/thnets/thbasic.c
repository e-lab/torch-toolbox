#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include "thnets.h"

#define THAtomicIncrement(a) __sync_fetch_and_add(a, 1);
#define THAtomicDecrement(a) __sync_fetch_and_add(a, -1);

THFloatStorage *THFloatStorage_new(long size)
{
	THFloatStorage *s = malloc(sizeof(*s));
	s->data = malloc(sizeof(*s->data) * size);
	s->nref = 1;
	s->mustfree = 1;
	return s;
}

THFloatStorage *THFloatStorage_newwithbuffer(void *buffer)
{
	THFloatStorage *s = malloc(sizeof(*s));
	s->data = buffer;
	s->nref = 1;
	s->mustfree = 0;
	return s;
}

void THFloatStorage_free(THFloatStorage *s)
{
	THAtomicDecrement(&s->nref);
	if(s->nref == 0)
	{
		if(s->mustfree)
			free(s->data);
		free(s);
	}
}

void THFloatTensor_resize(THFloatTensor *t, long *size, int nDimension)
{
	int i;
	long stride = 1;
	
	t->nDimension = nDimension;
	memcpy(t->size, size, nDimension * sizeof(*t->size));
	for(i = nDimension - 1; i >= 0; i--)
	{
		t->stride[i] = stride;
		stride *= t->size[i];
	}
	if(!t->storage)
		t->storage = THFloatStorage_new(stride);
}

void THFloatTensor_resize4d(THFloatTensor *t, long size0, long size1, long size2, long size3)
{
	t->nDimension = 4;
	t->size[0] = size0;
	t->size[1] = size1;
	t->size[2] = size2;
	t->size[3] = size3;
	t->stride[3] = 1;
	t->stride[2] = size3;
	t->stride[1] = size2 * size3;
	t->stride[0] = size1 * size2 * size3;
	if(!t->storage)
		t->storage = THFloatStorage_new(size0 * size1 * size2 * size3);
}

void THFloatTensor_resize3d(THFloatTensor *t, long size0, long size1, long size2)
{
	t->nDimension = 3;
	t->size[0] = size0;
	t->size[1] = size1;
	t->size[2] = size2;
	t->stride[2] = 1;
	t->stride[1] = size2;
	t->stride[0] = size1 * size2;
	if(!t->storage)
		t->storage = THFloatStorage_new(size0 * size1 * size2);
}

void THFloatTensor_resize2d(THFloatTensor *t, long size0, long size1)
{
	t->nDimension = 2;
	t->size[0] = size0;
	t->size[1] = size1;
	t->stride[1] = 1;
	t->stride[0] = size1;
	if(!t->storage)
		t->storage = THFloatStorage_new(size0 * size1);
}

void THFloatTensor_resize1d(THFloatTensor *t, long size0)
{
	t->nDimension = 1;
	t->size[0] = size0;
	t->stride[0] = 1;
	if(!t->storage)
		t->storage = THFloatStorage_new(size0);
}

void THError(const char *fmt, ...)
{
	va_list ap;
	
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(-1);
}

void THFloatTensor_free(THFloatTensor *t)
{
	if(t->storage)
		THFloatStorage_free(t->storage);
	free(t);
}

THFloatTensor *THFloatTensor_newSelect(THFloatTensor *tensor, int dimension, long sliceIndex)
{
	if(dimension)
		THError("THFloatTensor_newSelect not implemented for dimension != 0");
	THFloatTensor *t = malloc(sizeof(*t));
	t->nDimension = tensor->nDimension - 1;
	t->size[0] = tensor->size[1];
	t->size[1] = tensor->size[2];
	t->size[2] = tensor->size[3];
	t->stride[0] = tensor->stride[1];
	t->stride[1] = tensor->stride[2];
	t->stride[2] = tensor->stride[3];
	t->storage = tensor->storage;
	THAtomicIncrement(&t->storage->nref);
	t->storageOffset = sliceIndex * tensor->stride[0];
	return t;
}

long THFloatTensor_nElement(THFloatTensor *t)
{
	if(t->nDimension == 0)
		return 0;
	else
	{
		long nElement = 1;
		int i;
		for(i = 0; i < t->nDimension; i++)
			nElement *= t->size[i];
		return nElement;
	}
}

void THFloatTensor_resizeAs(THFloatTensor *tdst, THFloatTensor *tsrc)
{
	if(tsrc == tdst)
		return;
	long nelemsrc = THFloatTensor_nElement(tsrc);
	tdst->nDimension = tsrc->nDimension;
	memcpy(tdst->size, tsrc->size, sizeof(tsrc->size));
	memcpy(tdst->stride, tsrc->stride, sizeof(tsrc->stride));
	if(!tdst->storage)
		tdst->storage = THFloatStorage_new(nelemsrc);
	else if(nelemsrc != THFloatTensor_nElement(tdst))
	{
		if(tdst->storage)
			tdst->storage->data = realloc(tdst->storage->data, sizeof(*tdst->storage->data) * nelemsrc);
		else tdst->storage = THFloatStorage_new(nelemsrc);
	}
}

void THFloatTensor_set(THFloatTensor *tdst, THFloatTensor *tsrc)
{
	if(tsrc == tdst)
		return;
	if(tdst->storage)
		THFloatStorage_free(tdst->storage);
	*tdst = *tsrc;
	THAtomicIncrement(&tsrc->storage->nref);
}

float *THFloatTensor_data(THFloatTensor *tensor)
{
	return tensor->storage->data + tensor->storageOffset;
}

THFloatTensor *THFloatTensor_new()
{
	return calloc(1, sizeof(THFloatTensor));
}

THFloatTensor *THFloatTensor_newWithStorage2d(THFloatStorage *storage, long storageOffset, long size0, long stride0, long size1, long stride1)
{
	THFloatTensor *t = THFloatTensor_new();
	t->nDimension = 2;
	t->size[0] = size0;
	t->size[1] = size1;
	t->stride[0] = stride0 == -1 ? size1 : stride0;
	t->stride[1] = stride1 == -1 ? 1 : stride1;
	t->storage = storage;
	t->storageOffset = storageOffset;
	THAtomicIncrement(&t->storage->nref);
	return t;
}

THFloatTensor *THFloatTensor_newWithStorage1d(THFloatStorage *storage, long storageOffset, long size0, long stride0)
{
	THFloatTensor *t = THFloatTensor_new();
	t->nDimension = 1;
	t->size[0] = size0;
	t->stride[0] = stride0 == -1 ? 1 : stride0;
	t->storage = storage;
	t->storageOffset = storageOffset;
	THAtomicIncrement(&t->storage->nref);
	return t;
}

THFloatTensor *THFloatTensor_newWithTensor(THFloatTensor *tensor)
{
	THFloatTensor *self = THFloatTensor_new();
	THFloatTensor_set(self, tensor);
	return self;
}

void THFloatVector_fill(float *x, float c, long n)
{
	long i = 0;
	for(; i < n-4; i += 4)
	{
		x[i] = c;
		x[i+1] = c;
		x[i+2] = c;
		x[i+3] = c;
	}

	for(; i < n; i++)
		x[i] = c;
}

void THFloatTensor_zero(THFloatTensor *t)
{
	memset(t->storage->data, 0, THFloatTensor_nElement(t) * sizeof(*t->storage->data));
}

void THFloatTensor_fill(THFloatTensor *t, float value)
{
	THFloatVector_fill(t->storage->data, value, THFloatTensor_nElement(t));
}

void THFloatTensor_copy(THFloatTensor *tdst, THFloatTensor *tsrc)
{
	memcpy(tdst->storage->data, tsrc->storage->data, sizeof(*tdst->storage->data) * THFloatTensor_nElement(tsrc));
}

void THFloatTensor_transpose(THFloatTensor *tdst, THFloatTensor *tsrc, int dimension1, int dimension2)
{
	long z;

	if(!tsrc)
		tsrc = tdst;

	THFloatTensor_set(tdst, tsrc);

	if(dimension1 == dimension2)
		return;

	z = tdst->stride[dimension1];
	tdst->stride[dimension1] = tdst->stride[dimension2];
	tdst->stride[dimension2] = z;
	z = tdst->size[dimension1];
	tdst->size[dimension1] = tdst->size[dimension2];
	tdst->size[dimension2] = z;
}

THFloatTensor *THFloatTensor_newTranspose(THFloatTensor *tensor, int dimension1_, int dimension2_)
{
  THFloatTensor *self = THFloatTensor_newWithTensor(tensor);
  THFloatTensor_transpose(self, NULL, dimension1_, dimension2_);
  return self;
}


double THExpMinusApprox(double x)
{
#if EXACT_EXPONENTIAL
  return exp(-x);
#else
  /* fast approximation of exp(-x) for x positive */
# define A0   (1.0)
# define A1   (0.125)
# define A2   (0.0078125)
# define A3   (0.00032552083)
# define A4   (1.0172526e-5)
  if (x < 13.0)
  {
/*    assert(x>=0); */
    double y;
    y = A0+x*(A1+x*(A2+x*(A3+x*A4)));
    y *= y;
    y *= y;
    y *= y;
    y = 1/y;
    return y;
  }
  return 0;
# undef A0
# undef A1
# undef A2
# undef A3
# undef A4
#endif
}


#define INT_MAX 0x7fffffff
extern void sgemm_(char *transa, char *transb, int *m, int *n, int *k, float *alpha, float *a, int *lda, float *b, int *ldb, float *beta, float *c, int *ldc);
static void THBlas_gemm(char transa, char transb, long m, long n, long k, float alpha, float *a, long lda, float *b, long ldb, float beta, float *c, long ldc)
{
	int transa_ = ((transa == 't') || (transa == 'T'));
	int transb_ = ((transb == 't') || (transb == 'T'));

	if(n == 1)
		ldc = m;

	if(transa_)
	{
		if(m == 1)
			lda = k;
	}
	else
	{
		if(k == 1)
			lda = m;
	}

	if(transb_)
	{
		if(k == 1)
			ldb = n;
	}
	else
	{
		if(n == 1)
			ldb = k;
	}

	if( (m <= INT_MAX) && (n <= INT_MAX) && (k <= INT_MAX) && (lda <= INT_MAX)  && (ldb <= INT_MAX) && (ldc <= INT_MAX) )
	{
		int i_m = (int)m;
		int i_n = (int)n;
		int i_k = (int)k;
		int i_lda = (int)lda;
		int i_ldb = (int)ldb;
		int i_ldc = (int)ldc;

		sgemm_(&transa, &transb, &i_m, &i_n, &i_k, &alpha, a, &i_lda, b, &i_ldb, &beta, c, &i_ldc);
		return;
	}
	THError("Wrong parameters to gemm");
}

void sgemv_(char *trans, int *m, int *n, float *alpha, float *a, int *lda, float *x, int *incx, float *beta, float *y, int *incy);
void THBlas_gemv(char trans, long m, long n, float alpha, float *a, long lda, float *x, long incx, float beta, float *y, long incy)
{
	if(n == 1)
		lda = m;
  
	if( (m <= INT_MAX) && (n <= INT_MAX) && 
		(lda > 0) && (lda <= INT_MAX) &&
		(incx > 0) && (incx <= INT_MAX) &&
		(incy > 0) && (incy <= INT_MAX) )
	{
		int i_m = (int)m;
		int i_n = (int)n;
		int i_lda = (int)lda;
		int i_incx = (int)incx;
		int i_incy = (int)incy;

		sgemv_(&trans, &i_m, &i_n, &alpha, a, &i_lda, x, &i_incx, &beta, y, &i_incy);
		return;
	}
}

void sger_(int *m, int *n, float *alpha, float *x, int *incx, float *y, int *incy, float *a, int *lda);
void THBlas_ger(long m, long n, float alpha, float *x, long incx, float *y, long incy, float *a, long lda)
{
	if(n == 1)
		lda = m;

	int i_m = (int)m;
	int i_n = (int)n;
	int i_lda = (int)lda;
	int i_incx = (int)incx;
	int i_incy = (int)incy;

	sger_(&i_m, &i_n, &alpha, x, &i_incx, y, &i_incy, a, &i_lda);
}

void THFloatTensor_addmm(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *m1, THFloatTensor *m2)
{ 
	char transpose_r, transpose_m1, transpose_m2;
	THFloatTensor *r__, *m1_, *m2_;

	if( (m1->nDimension != 2) || (m2->nDimension != 2))
		THError("matrices expected, got %dD, %dD tensors", m1->nDimension, m2->nDimension);

	if(m1->size[1] != m2->size[0])
		THError("size mismatch, m1: %ld, m2: %ld", m1->size[1], m2->size[0]);

	if( t->nDimension != 2 )
		THError("matrix expected, got %dD tensor for t", t->nDimension);

	if( (t->size[0] != m1->size[0]) || (t->size[1] != m2->size[1]) )
		THError("size mismatch, t: %ld, m1: %ld, t: %ld, m2: %ld", t->size[0], m1->size[1], t->size[1], m2->size[1]);

	if(t != r_)
		THError("Not implemented: t != r");

	/*  printf("%ldx%ld = %ldx%ld X %ldx%ld\n", r_->size[0], r_->size[1], m1->size[0], m1->size[1], m2->size[0], m2->size[1]); */

	/* r_ */
	if(r_->stride[0] == 1 && r_->stride[1] != 0)
	{
		transpose_r = 'n';
		r__ = r_;
	}
	else if(r_->stride[1] == 1 && r_->stride[0] != 0)
	{
		THFloatTensor *swap = m2;
		m2 = m1;
		m1 = swap;
		transpose_r = 't';
		r__ = r_;
	}
	else
	{
		THError("Transpose not implemented (1)");
/*		transpose_r = 'n';

		r__ = THFloatTensor_newWithSize2d(r_->size[1], r_->size[0]);
		THFloatTensor_copy(r__, r_);
		THFloatTensor_transpose(r__, NULL, 0, 1);*/
	}

	/* m1 */
	if(m1->stride[(transpose_r == 'n' ? 0 : 1)] == 1 && m1->stride[(transpose_r == 'n' ? 1 : 0)] != 0)
	{
		transpose_m1 = 'n';
		m1_ = m1;
	}
	else if(m1->stride[(transpose_r == 'n' ? 1 : 0)] == 1 && m1->stride[(transpose_r == 'n' ? 0 : 1)] != 0)
	{
		transpose_m1 = 't';
		m1_ = m1;
	}
	else
	{
		THError("Transpose not implemented (2)");
		/*transpose_m1 = (transpose_r == 'n' ? 't' : 'n');
		m1_ = THFloatTensor_newContiguous(m1);*/
	}

	/* m2 */
	if(m2->stride[(transpose_r == 'n' ? 0 : 1)] == 1 && m2->stride[(transpose_r == 'n' ? 1 : 0)] != 0)
	{
		transpose_m2 = 'n';
		m2_ = m2;
	}
	else if(m2->stride[(transpose_r == 'n' ? 1 : 0)] == 1 && m2->stride[(transpose_r == 'n' ? 0 : 1)] != 0)
	{
		transpose_m2 = 't';
		m2_ = m2;
	}
	else
	{
		THError("Transpose not implemented (3)");
		/*transpose_m2 = (transpose_r == 'n' ? 't' : 'n');
		m2_ = THFloatTensor_(newContiguous)(m2);*/
	}

	/* do the operation */
	THBlas_gemm(transpose_m1,
		transpose_m2,
		r__->size[(transpose_r == 'n' ? 0 : 1)],
		r__->size[(transpose_r == 'n' ? 1 : 0)],
		m1_->size[(transpose_r == 'n' ? 1 : 0)],
		alpha,
		THFloatTensor_data(m1_),
		(transpose_m1 == 'n' ? m1_->stride[(transpose_r == 'n' ? 1 : 0)] : m1_->stride[(transpose_r == 'n' ? 0 : 1)]),
		THFloatTensor_data(m2_),
		(transpose_m2 == 'n' ? m2_->stride[(transpose_r == 'n' ? 1 : 0)] : m2_->stride[(transpose_r == 'n' ? 0 : 1)]),
		beta,
		THFloatTensor_data(r__),
		r__->stride[(transpose_r == 'n' ? 1 : 0)]);

	/* free intermediate variables */
	if(m1_ != m1)
		THFloatTensor_free(m1_);

	if(m2_ != m2)
		THFloatTensor_free(m2_);

	if(r__ != r_)
		THError("freeCopyTo not implemented"); 
		/*THFloatTensor_(freeCopyTo)(r__, r_);*/
} 

void THFloatTensor_addmv(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *mat, THFloatTensor *vec)
{
	if( (mat->nDimension != 2) || (vec->nDimension != 1) )
		THError("matrix and vector expected, got %dD, %dD", mat->nDimension, vec->nDimension);

	if( mat->size[1] != vec->size[0] )
		THError("size mismatch, %s, %s", mat->size[1], vec->size[0]);

	if(t->nDimension != 1) 
		THError("vector expected, got t: %dD", t->nDimension);

	if(t->size[0] != mat->size[0])
		THError("size mismatch, t: %ld, mat: %ld", t->size[0], mat->size[0]);

	if(r_ != t)
		THError("r_ != t not implemented");

	if(mat->stride[0] == 1)
	{
		THBlas_gemv('n', mat->size[0], mat->size[1], alpha, THFloatTensor_data(mat), mat->stride[1],
			THFloatTensor_data(vec), vec->stride[0], beta, THFloatTensor_data(r_), r_->stride[0]);
	}
	else if(mat->stride[1] == 1)
	{
		THBlas_gemv('t',  mat->size[1], mat->size[0], alpha, THFloatTensor_data(mat), mat->stride[0],
			THFloatTensor_data(vec), vec->stride[0], beta, THFloatTensor_data(r_), r_->stride[0]);
	}
	else THError("addmv for non-contiguous not implemented");
}

#define TH_OMP_OVERHEAD_THRESHOLD 100000

void THFloatTensor_mul(THFloatTensor *r_, THFloatTensor *t, float value)
{
	float *tp = THFloatTensor_data(t);
	float *rp = THFloatTensor_data(r_);
	long i;
	long sz = THFloatTensor_nElement(t);

#pragma omp parallel for if(sz > TH_OMP_OVERHEAD_THRESHOLD) private(i)
	for (i=0; i<sz; i++)
		rp[i] = tp[i] * value;
}

void THFloatTensor_addr(THFloatTensor *r_, float beta, THFloatTensor *t, float alpha, THFloatTensor *vec1, THFloatTensor *vec2)
{
	if( (vec1->nDimension != 1) || (vec2->nDimension != 1) )
		THError("vector and vector expected, got %dD, %dD tensors", vec1->nDimension, vec2->nDimension);

	if(t->nDimension != 2)
		THError("expected matrix, got %dD tensor for t", t->nDimension);
    
	if( (t->size[0] != vec1->size[0]) || (t->size[1] != vec2->size[0]) )
		THError("size mismatch, t: %ld, vec1: %ld, t: %ld, vec2: %ld", t->size[0], vec1->size[0], t->size[1], vec2->size[0]);

	if(r_ != t)
		THError("r_ != t not implemented");

	if(beta != 1)
		THFloatTensor_mul(r_, r_, beta);

  if(r_->stride[0] == 1)
  {
    THBlas_ger(vec1->size[0], vec2->size[0],
                 alpha, THFloatTensor_data(vec1), vec1->stride[0],
                 THFloatTensor_data(vec2), vec2->stride[0],
                 THFloatTensor_data(r_), r_->stride[1]);
  }
  else if(r_->stride[1] == 1)
  {
    THBlas_ger(vec2->size[0], vec1->size[0],
                 alpha, THFloatTensor_data(vec2), vec2->stride[0],
                 THFloatTensor_data(vec1), vec1->stride[0],
                 THFloatTensor_data(r_), r_->stride[0]);
  }
  else THError("addr for non-contiguous not implemented");
}

#include <luaT.h>
#include <TH/TH.h>
#include <TH/THFilePrivate.h>

typedef struct THFile__ THFile;
typedef struct THDiskFile__
{
    THFile file;

    FILE *handle;
    char *name;
    int isNativeEncoding;

} THDiskFile;

typedef struct THMemoryFile__
{
    THFile file;
    THCharStorage *storage;
    long size;
    long position;

} THMemoryFile;



/* workaround mac osx lion ***insane*** fread bug */
#ifdef __APPLE__
size_t fread__(void *ptr, size_t size, size_t nitems, FILE *stream)
{
  size_t nread = 0;
  while(!feof(stream) && !ferror(stream) && (nread < nitems))
    nread += fread((char*)ptr+nread*size, size, THMin(2147483648/size, nitems-nread), stream);
  return nread;
}
#else
#define fread__ fread
#endif


// taken from https://github.com/torch/torch7/blob/master/lib/TH/THDiskFile.c
// and modified to work on 32 bit machine
#define DISK_LONG_READ_WRITE_METHODS(TYPE, TYPEC, ASCII_READ_ELEM, ASCII_WRITE_ELEM) \
  static long THDiskFile_read##TYPEC(THFile *self, TYPE *data, long n)  \
  {                                                                     \
    THDiskFile *dfself = (THDiskFile*)(self);                           \
    long nread = 0L;                                                    \
                                                                        \
    THArgCheck(dfself->handle != NULL, 1, "attempt to use a closed file"); \
    THArgCheck(dfself->file.isReadable, 1, "attempt to read in a write-only file"); \
                                                                        \
    if(dfself->file.isBinary)                                           \
    {                                                                   \
      int dummy, i, rb;                                                 \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        rb = fread__(data+i, 4, 1, dfself->handle);                     \
        rb = fread__(&dummy, 4, 1, dfself->handle);                     \
        if(!dfself->isNativeEncoding && rb > 0)                         \
          THDiskFile_reverseMemory(data+i, data+i, 4, 1);               \
        nread += rb;                                                    \
      }                                                                 \
    }                                                                   \
    else                                                                \
    {                                                                   \
      long i;                                                           \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        ASCII_READ_ELEM; /* increment here result and break if wrong */ \
      }                                                                 \
      if(dfself->file.isAutoSpacing && (n > 0))                         \
      {                                                                 \
        int c = fgetc(dfself->handle);                                  \
        if( (c != '\n') && (c != EOF) )                                 \
          ungetc(c, dfself->handle);                                    \
      }                                                                 \
    }                                                                   \
                                                                        \
    if(nread != n)                                                      \
    {                                                                   \
      dfself->file.hasError = 1; /* shouldn't we put hasError to 0 all the time ? */ \
      if(!dfself->file.isQuiet)                                         \
        THError("read error: read %d blocks instead of %d", nread, n);  \
    }                                                                   \
                                                                        \
    return nread;                                                       \
  }                                                                     \
                                                                        \
  static long THDiskFile_write##TYPEC(THFile *self, TYPE *data, long n) \
  {                                                                     \
    THDiskFile *dfself = (THDiskFile*)(self);                           \
    long nwrite = 0L;                                                   \
                                                                        \
    THArgCheck(dfself->handle != NULL, 1, "attempt to use a closed file"); \
    THArgCheck(dfself->file.isWritable, 1, "attempt to write in a read-only file"); \
                                                                        \
    if(dfself->file.isBinary)                                           \
    {                                                                   \
      int i, zero = 0, tmpdata;                                         \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        if(!dfself->isNativeEncoding)                                   \
        {                                                               \
          THDiskFile_reverseMemory(&tmpdata, data+i, 4, 1);             \
          nwrite += fwrite(&tmpdata, 4, 1, dfself->handle);             \
          fwrite(&zero, 4, 1, dfself->handle);                          \
        } else {                                                        \
          nwrite += fwrite(data+i, 4, 1, dfself->handle);               \
          fwrite(&zero, 4, 1, dfself->handle);                          \
        }                                                               \
      }                                                                 \
    }                                                                   \
    else                                                                \
    {                                                                   \
      long i;                                                           \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        ASCII_WRITE_ELEM;                                               \
        if( dfself->file.isAutoSpacing && (i < n-1) )                   \
          fprintf(dfself->handle, " ");                                 \
      }                                                                 \
      if(dfself->file.isAutoSpacing && (n > 0))                         \
        fprintf(dfself->handle, "\n");                                  \
    }                                                                   \
                                                                        \
    if(nwrite != n)                                                     \
    {                                                                   \
      dfself->file.hasError = 1;                                        \
      if(!dfself->file.isQuiet)                                         \
        THError("write error: wrote %d blocks instead of %d", nwrite, n); \
    }                                                                   \
                                                                        \
    return nwrite;                                                      \
}


static void THDiskFile_reverseMemory(void *dst, const void *src, long blockSize, long numBlocks)
{
  if(blockSize != 1)
  {
    long halfBlockSize = blockSize/2;
    char *charSrc = (char*)src;
    char *charDst = (char*)dst;
    long b, i;
    for(b = 0; b < numBlocks; b++)
    {
      for(i = 0; i < halfBlockSize; i++)
      {
        char z = charSrc[i];
        charDst[i] = charSrc[blockSize-1-i];
        charDst[blockSize-1-i] = z;
      }
      charSrc += blockSize;
      charDst += blockSize;
    }
  }
}


// use macro to generate THDiskFile_read/wrte for 'long' type on 32bit machine
DISK_LONG_READ_WRITE_METHODS(long, Long,
   int ret = fscanf(dfself->handle, "%ld", &data[i]); if(ret <= 0) break; else nread++,
   int ret = fprintf(dfself->handle, "%ld", data[i]); if(ret <= 0) break; else nwrite++)



// taken from https://github.com/torch/torch7/blob/master/lib/TH/THMemoryFile.c
// and modified to work on 32 bit machine
#define MEM_LONG_READ_WRITE_METHODS(TYPE, TYPEC, ASCII_READ_ELEM, ASCII_WRITE_ELEM, INSIDE_SPACING) \
  static long THMemoryFile_read##TYPEC(THFile *self, TYPE *data, long n) \
  {                                                                     \
    THMemoryFile *mfself = (THMemoryFile*)self;                         \
    long nread = 0L;                                                    \
                                                                        \
    THArgCheck(mfself->storage != NULL, 1, "attempt to use a closed file");     \
    THArgCheck(mfself->file.isReadable, 1, "attempt to read in a write-only file"); \
                                                                        \
    if(mfself->file.isBinary)                                           \
    {                                                                   \
      long nByte = 8*n;                                                 \
      long nByteRemaining = (mfself->position + nByte <= mfself->size ? nByte : mfself->size-mfself->position); \
      nread = nByteRemaining/8;                                         \
      int i;                                                            \
      for(i = 0; i < nread; i++)                                        \
      {                                                                 \
        memmove(data+i, mfself->storage->data+mfself->position, 4);     \
        mfself->position += 8;                                          \
      }                                                                 \
    }                                                                   \
    else                                                                \
    {                                                                   \
      long i;                                                           \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        long nByteRead = 0;                                             \
        char spaceChar = 0;                                             \
        char *spacePtr = THMemoryFile_strnextspace(mfself->storage->data+mfself->position, &spaceChar); \
        ASCII_READ_ELEM;                                                \
        if(ret == EOF)                                                  \
        {                                                               \
          while(mfself->storage->data[mfself->position])                \
            mfself->position++;                                         \
        }                                                               \
        else                                                            \
          mfself->position += nByteRead;                                \
        if(spacePtr)                                                    \
          *spacePtr = spaceChar;                                        \
      }                                                                 \
      if(mfself->file.isAutoSpacing && (n > 0))                         \
      {                                                                 \
        if( (mfself->position < mfself->size) && (mfself->storage->data[mfself->position] == '\n') ) \
          mfself->position++;                                           \
      }                                                                 \
    }                                                                   \
                                                                        \
    if(nread != n)                                                      \
    {                                                                   \
      mfself->file.hasError = 1; /* shouldn't we put hasError to 0 all the time ? */ \
      if(!mfself->file.isQuiet)                                         \
        THError("read error: read %d blocks instead of %d", nread, n);  \
    }                                                                   \
                                                                        \
    return nread;                                                       \
  }                                                                     \
                                                                        \
  static long THMemoryFile_write##TYPEC(THFile *self, TYPE *data, long n) \
  {                                                                     \
    THMemoryFile *mfself = (THMemoryFile*)self;                         \
                                                                        \
    THArgCheck(mfself->storage != NULL, 1, "attempt to use a closed file");     \
    THArgCheck(mfself->file.isWritable, 1, "attempt to write in a read-only file"); \
                                                                        \
    if(mfself->file.isBinary)                                           \
    {                                                                   \
      long nByte = 8*n;                                                 \
      int i;                                                            \
      THMemoryFile_grow(mfself, mfself->position+nByte);                \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
         ((long *)(mfself->storage->data+mfself->position))[2*i] = data[i]; \
         ((long *)(mfself->storage->data+mfself->position))[2*i+1] = 0;   \
      }                                                                 \
      mfself->position += nByte;                                        \
      if(mfself->position > mfself->size)                               \
      {                                                                 \
        mfself->size = mfself->position;                                \
        mfself->storage->data[mfself->size] = '\0';                     \
      }                                                                 \
    }                                                                   \
    else                                                                \
    {                                                                   \
      long i;                                                           \
      for(i = 0; i < n; i++)                                            \
      {                                                                 \
        long nByteWritten;                                              \
        while (1)                                                       \
        {                                                               \
          ASCII_WRITE_ELEM;                                             \
          if( (nByteWritten > -1) && (nByteWritten < mfself->storage->size-mfself->position) ) \
          {                                                             \
            mfself->position += nByteWritten;                           \
            break;                                                      \
          }                                                             \
          THMemoryFile_grow(mfself, mfself->storage->size + (mfself->storage->size/2) + 2); \
        }                                                               \
        if(mfself->file.isAutoSpacing)                                  \
        {                                                               \
          if(i < n-1)                                                   \
          {                                                             \
            THMemoryFile_grow(mfself, mfself->position+1);              \
            sprintf(mfself->storage->data+mfself->position, " ");       \
            mfself->position++;                                         \
          }                                                             \
          if(i == n-1)                                                  \
          {                                                             \
            THMemoryFile_grow(mfself, mfself->position+1);              \
            sprintf(mfself->storage->data+mfself->position, "\n");      \
            mfself->position++;                                         \
          }                                                             \
        }                                                               \
      }                                                                 \
      if(mfself->position > mfself->size)                               \
      {                                                                 \
        mfself->size = mfself->position;                                \
        mfself->storage->data[mfself->size] = '\0';                     \
      }                                                                 \
    }                                                                   \
                                                                        \
    return n;                                                           \
  }


static char *THMemoryFile_strnextspace(char *str_, char *c_)
{
  char c;

  while( (c = *str_) )
  {
    if( (c != ' ') && (c != '\n') && (c != ':') && (c != ';') )
      break;
    str_++;
  }

  while( (c = *str_) )
  {
    if( (c == ' ') || (c == '\n') || (c == ':') || (c == ';') )
    {
      *c_ = c;
      *str_ = '\0';
      return(str_);
    }
    str_++;
  }
  return NULL;
}


static void THMemoryFile_grow(THMemoryFile *self, long size)
{
  long missingSpace;

  if(size <= self->size)
    return;
  else
  {
    if(size < self->storage->size) /* note the "<" and not "<=" */
    {
      self->size = size;
      self->storage->data[self->size] = '\0';
      return;
    }
  }

  missingSpace = size-self->storage->size+1; /* +1 for the '\0' */
  THCharStorage_resize(self->storage, (self->storage->size/2 > missingSpace ?
                                       self->storage->size + (self->storage->size/2)
                                       : self->storage->size + missingSpace));
}


// use macro to generate THMemoryFile_read/wrte for 'long' type on 32bit machine
MEM_LONG_READ_WRITE_METHODS(long, Long,
                            int nByteRead_; int ret = sscanf(mfself->storage->data+mfself->position, "%ld%n", &data[i], &nByteRead_); nByteRead = nByteRead_; if(ret <= 0) break; else nread++,
                            nByteWritten = snprintf(mfself->storage->data+mfself->position, mfself->storage->size-mfself->position, "%ld", data[i]),
                            1)


// override write/read functions for long data type
int luaopen_libbincompat(lua_State * L)
{
   THFile *self_disk = THDiskFile_new("/tmp", "r", 0);
   self_disk->vtable->writeLong = THDiskFile_writeLong;
   self_disk->vtable->readLong = THDiskFile_readLong;

   THFile *self_mem = THMemoryFile_new("rw");
   self_mem->vtable->writeLong = THMemoryFile_writeLong;
   self_mem->vtable->readLong = THMemoryFile_readLong;
   return 1;
}

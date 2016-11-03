#include <stdio.h>

void *debug_malloc(size_t size, const char *file, int line);
void *debug_calloc(size_t nmemb, size_t size, const char *file, int line);
void *debug_realloc(void *ptr, size_t size, const char *file, int line);
char *debug_strdup(const char *str, const char *file, int line);
void debug_free(void *ptr, const char *file, int line);
void debug_memorydump(FILE *fp);

#define malloc(a) debug_malloc(a,__FILE__,__LINE__)
#define calloc(a,b) debug_calloc(a,b,__FILE__,__LINE__)
#define realloc(a,b) debug_realloc(a,b,__FILE__,__LINE__)
#define strdup(a) debug_strdup(a,__FILE__,__LINE__)
#define free(a) debug_free(a,__FILE__,__LINE__)

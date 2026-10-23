#define DEFINE_MALLOC
#define DEFINE_FREE
#define DEFINE_CALLOC
#define DEFINE_REALLOC
#define DEFINE_MALLOC_USABLE_SIZE
#define DEFINE_MEMALIGN

#include <stddef.h>

void * _sbrk_r(ptrdiff_t increment);

#include "nano-mallocr.c"

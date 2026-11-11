#define DEFINE_MALLOC
#define DEFINE_FREE
#define DEFINE_CALLOC
#define DEFINE_REALLOC
#define DEFINE_MALLOC_USABLE_SIZE
#define DEFINE_MEMALIGN

#include <stddef.h>
#include <malloc.h>

#include "allocator_core.h"

#define malloc Allocator__malloc_unlocked
#define free Allocator__free_unlocked
#define calloc Allocator__calloc_unlocked
#define realloc Allocator__realloc_unlocked
#define malloc_usable_size Allocator__usable_size_unlocked
#define memalign Allocator__memalign_unlocked

void * _sbrk_r(ptrdiff_t increment);

#include "nano-mallocr.c"

#ifndef __USER_SYS_MMIO_H
#define __USER_SYS_MMIO_H

#include <stddef.h>
#include <stdint.h>

void __sync_boundary();
void __sync_cache_clean(void * address, size_t size);
void __sync_cache_invalidate(void * address, size_t size);
void __sync_cache_clean_invalidate(void * address, size_t size);

#endif

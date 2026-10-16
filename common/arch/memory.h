#ifndef __COMMON_ARCH_MEMORY_H
#define __COMMON_ARCH_MEMORY_H

#include <stddef.h>

void * ArchMemory__memcpy(void * self, const void * source, size_t size);
void * ArchMemory__memset(void * self, int value, size_t size);

#endif

#ifndef __COMMON_MEMORY_H
#define __COMMON_MEMORY_H

#include <stddef.h>

void * Mem__memcpy(void * self, const void * source, size_t size);
void * Mem__memmove(void * self, const void * source, size_t size);
void * Mem__memset(void * self, int value, size_t size);
int Mem__memcmp(const void * self, const void * other, size_t size);

#endif

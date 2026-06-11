#ifndef __USER_LIB_NEWLIB_STRING_H
#define __USER_LIB_NEWLIB_STRING_H

#include <stddef.h>

void * memset(void * dest, int value, size_t size);
void * memcpy(void * dest, const void * src, size_t size);

#endif

#ifndef __USER_LIB_NEWLIB_STRINGS_H
#define __USER_LIB_NEWLIB_STRINGS_H

#include <stddef.h>

void bcopy(const void * source, void * destination, size_t size);
int strcasecmp(const char * left, const char * right);
int strncasecmp(const char * left, const char * right, size_t size);

#endif

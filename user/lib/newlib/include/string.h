#ifndef __USER_LIB_NEWLIB_STRING_H
#define __USER_LIB_NEWLIB_STRING_H

#include <stddef.h>

void * memset(void * dest, int value, size_t size);
void * memcpy(void * dest, const void * src, size_t size);
void * memmove(void * dest, const void * src, size_t size);
void * memchr(const void * buffer, int character, size_t size);
size_t strlen(const char * text);
int strcmp(const char * left, const char * right);
int strncmp(const char * left, const char * right, size_t size);
int memcmp(const void * left, const void * right, size_t size);
char * strcpy(char * dest, const char * source);
char * strncat(char * dest, const char * source, size_t size);
char * strchr(const char * text, int character);
char * strrchr(const char * text, int character);
size_t strspn(const char * text, const char * accept);
size_t strcspn(const char * text, const char * reject);
size_t strlcpy(char * dest, const char * source, size_t size);

#endif

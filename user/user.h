#ifndef __USER_USER_H_
#define __USER_USER_H_

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

#include "../common/types/bstring.h"

typedef int64_t ByteCount;

void print(const char * str);
int printf(const char * format, ...);
void * _sbrk(ptrdiff_t increment);
void * malloc(size_t size);
void free(void * address);
void * calloc(size_t n, size_t elem);
void * realloc(void * ptr, size_t size);

ByteCount console_read(char * buffer, uint64_t size);
ByteCount console_read_line(char * buffer, bool echo, uint64_t size);
BString Console__read(void);
BString Console__read_line(bool echo);
void exit(int code);

#endif

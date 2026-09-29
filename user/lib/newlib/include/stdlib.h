#ifndef __USER_LIB_NEWLIB_STDLIB_H
#define __USER_LIB_NEWLIB_STDLIB_H

#include <stddef.h>
#include <sys/types.h>

void * malloc(size_t size);
void free(void * address);
void * calloc(size_t count, size_t size);
void * realloc(void * address, size_t size);
void * reallocarray(void * address, size_t count, size_t size);
void exit(int code);
int atoi(const char * text);
unsigned long long strtoull(const char * text, char ** endptr, int base);
long long strtoll(const char * text, char ** endptr, int base);
void * recallocarray(void * address, size_t old_count, size_t new_count, size_t size);
const char * getprogname(void);
char * getenv(const char * name);
long long strtonum(const char * text, long long minimum, long long maximum, const char ** error);
void * setmode(const char * mode);
mode_t getmode(const void * set, mode_t mode);

#endif

#ifndef __USER_LIB_NEWLIB_STDLIB_H
#define __USER_LIB_NEWLIB_STDLIB_H

#include <stddef.h>
#include <sys/types.h>

#define MB_CUR_MAX (__locale_mb_cur_max())
#define RAND_MAX 2147483647

size_t __locale_mb_cur_max(void);

void * malloc(size_t size);
void free(void * address);
void * calloc(size_t count, size_t size);
void * realloc(void * address, size_t size);
void * reallocarray(void * address, size_t count, size_t size);
void abort(void) __dead;
void exit(int code);
int atoi(const char * text);
int rand(void);
void srand(unsigned int seed);
char * mktemp(char * path_template);
int mkstemp(char * path_template);
char * mkdtemp(char * path_template);
int mblen(const char * text, size_t size);
int mbtowc(wchar_t * character, const char * text, size_t size);
size_t mbstowcs(wchar_t * destination, const char * source, size_t size);
int wctomb(char * destination, wchar_t character);
size_t wcstombs(char * destination, const wchar_t * source, size_t size);
void qsort(void * base, size_t count, size_t size, int (*compare)(const void *, const void *));
long strtol(const char * text, char ** endptr, int base);
unsigned long strtoul(const char * text, char ** endptr, int base);
unsigned long long strtoull(const char * text, char ** endptr, int base);
long long strtoll(const char * text, char ** endptr, int base);
void * recallocarray(void * address, size_t old_count, size_t new_count, size_t size);
const char * getprogname(void);
extern char ** environ;
char * getenv(const char * name);
int setenv(const char * name, const char * value, int overwrite);
int unsetenv(const char * name);
int putenv(char * string);
long long strtonum(const char * text, long long minimum, long long maximum, const char ** error);
void * setmode(const char * mode);
mode_t getmode(const void * set, mode_t mode);

#endif

#ifndef __USER_LIB_NEWLIB_STDIO_H
#define __USER_LIB_NEWLIB_STDIO_H

#include <stdarg.h>
#include <stddef.h>

#define EOF (-1)
#define BUFSIZ 1024

#define _IONBF 2

typedef struct FILE FILE;

extern FILE * stdin;
extern FILE * stdout;
extern FILE * stderr;

FILE * fopen(const char * path, const char * mode);
int fclose(FILE * stream);
int getc(FILE * stream);
int getchar(void);
int putchar(int character);
int fileno(FILE * stream);
int ferror(FILE * stream);
void clearerr(FILE * stream);
int fflush(FILE * stream);
int setvbuf(FILE * stream, char * buffer, int mode, size_t size);
int fprintf(FILE * stream, const char * format, ...);
int vfprintf(FILE * stream, const char * format, va_list arguments);
int printf(const char * format, ...);
int snprintf(char * buffer, size_t size, const char * format, ...);

#endif

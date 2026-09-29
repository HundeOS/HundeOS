#ifndef __USER_LIB_NEWLIB_STDIO_H
#define __USER_LIB_NEWLIB_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

#define EOF (-1)
#define BUFSIZ 1024

#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

#define _IONBF 2
#define _IOLBF 1

typedef struct FILE FILE;

extern FILE * stdin;
extern FILE * stdout;
extern FILE * stderr;

FILE * fopen(const char * path, const char * mode);
FILE * fdopen(int descriptor, const char * mode);
int fclose(FILE * stream);
int getc(FILE * stream);
int ungetc(int character, FILE * stream);
int getchar(void);
int putchar(int character);
int fputs(const char * text, FILE * stream);
int fileno(FILE * stream);
int ferror(FILE * stream);
void clearerr(FILE * stream);
int feof(FILE * stream);
int fpurge(FILE * stream);
int fflush(FILE * stream);
int fseeko(FILE * stream, off_t offset, int whence);
int fseek(FILE * stream, long offset, int whence);
off_t ftello(FILE * stream);
void rewind(FILE * stream);
FILE * freopen(const char * path, const char * mode, FILE * stream);
size_t fread(void * buffer, size_t size, size_t count, FILE * stream);
size_t fwrite(const void * buffer, size_t size, size_t count, FILE * stream);
ssize_t getline(char ** line, size_t * capacity, FILE * stream);
int setvbuf(FILE * stream, char * buffer, int mode, size_t size);
int fprintf(FILE * stream, const char * format, ...);
int vfprintf(FILE * stream, const char * format, va_list arguments);
int printf(const char * format, ...);
int snprintf(char * buffer, size_t size, const char * format, ...);

#endif

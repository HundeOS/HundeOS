#ifndef __USER_LIB_NEWLIB_WCHAR_H
#define __USER_LIB_NEWLIB_WCHAR_H

#include <stddef.h>
#include <stdint.h>

typedef uint32_t wint_t;

typedef struct mbstate_t
{
    uint32_t codepoint;
    uint8_t expected;
    uint8_t seen;
} mbstate_t;

#define WEOF ((wint_t)-1)

wint_t btowc(int character);
int wctob(wint_t character);
int mbsinit(const mbstate_t * state);
size_t mbrlen(const char * text, size_t size, mbstate_t * state);
size_t mbrtowc(wchar_t * character, const char * text, size_t size, mbstate_t * state);
size_t mbsrtowcs(wchar_t * destination, const char ** source, size_t size, mbstate_t * state);
size_t wcrtomb(char * destination, wchar_t character, mbstate_t * state);
size_t wcsrtombs(char * destination, const wchar_t ** source, size_t size, mbstate_t * state);
size_t wcslen(const wchar_t * text);
wchar_t * wmemchr(const wchar_t * text, wchar_t character, size_t size);
wchar_t * wcschr(const wchar_t * text, wchar_t character);
wchar_t * wcscpy(wchar_t * destination, const wchar_t * source);
int wcscmp(const wchar_t * left, const wchar_t * right);
int wcscoll(const wchar_t * left, const wchar_t * right);
int wcsncmp(const wchar_t * left, const wchar_t * right, size_t size);
int wcwidth(wchar_t character);
int wcswidth(const wchar_t * text, size_t size);

#endif

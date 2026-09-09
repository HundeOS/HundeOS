#ifndef __KERNEL_PRINT_H
#define __KERNEL_PRINT_H
#include <stdarg.h>

int k_printf(const char * format, ...);
void k_println(const char *);
int k_sprintf(char * str, const char * format, ...);
int k_sprintf_impl(char * str, const char * format, va_list args);
#endif
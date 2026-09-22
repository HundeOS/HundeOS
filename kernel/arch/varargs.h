#ifndef __KERNEL_ARCH_VARARGS_H
#define __KERNEL_ARCH_VARARGS_H

#include <stdarg.h>
#include <stdbool.h>

bool KernelVarArgs__has_general_arg(va_list args);

#endif

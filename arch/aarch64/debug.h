#ifndef __AARCH64_DEBUG_H
#define __AARCH64_DEBUG_H

#include "../kernel/debug.h"
void dk_println(const char * str);
void dk_printf(const char * str, uint64_t val);

#endif
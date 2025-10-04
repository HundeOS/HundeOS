#ifndef __AARCH64_BOOT_PRINT_H_
#define __AARCH64_BOOT_PRINT_H_
#include <stdint.h>

#include "boot.h"

__BOOT__(void) _print(const char * str);
__BOOT__(void) _print_base(uint64_t value, const int base);
__BOOT__(void) _print_unsigned(uint64_t value);
__BOOT__(void) _print_binary(uint64_t value);

#endif
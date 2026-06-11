#ifndef __AARCH64_BOOT_PRINT_H_
#define __AARCH64_BOOT_PRINT_H_
#include <stdint.h>

#include "boot.h"

__BOOT__(void) _print(const char * str);
__BOOT__(void) _print_base(uint64_t value, const int base);
__BOOT__(void) _print_unsigned(uint64_t value);
__BOOT__(void) _print_binary(uint64_t value);

#ifdef __BOOT_DEBUG
#define boot_debug_print(str) _print(str)
#define boot_debug_print_base(value, base) _print_base((value), (base))
#define boot_debug_print_unsigned(value) _print_unsigned(value)
#define boot_debug_print_binary(value) _print_binary(value)
#else
#define boot_debug_print(str) do { if (0) _print(str); } while (0)
#define boot_debug_print_base(value, base) do { if (0) _print_base((value), (base)); } while (0)
#define boot_debug_print_unsigned(value) do { if (0) _print_unsigned(value); } while (0)
#define boot_debug_print_binary(value) do { if (0) _print_binary(value); } while (0)
#endif

#endif

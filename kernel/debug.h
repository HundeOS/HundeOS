#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

#include "print.h"
#include <stdint.h>

typedef enum {
    DEBUG = -1,
    INFO = 0,
    PROGRESS,
    UNHANDLED,
    FAULT,
    PANIC,
    FOCUS
} Level;

#define MIN_LEVEL PROGRESS

#define __d_printf(level, str, ...) \
if (level >= MIN_LEVEL) { k_printf(str, __VA_ARGS__); }

#define __d_println(level, str) \
if (level >= MIN_LEVEL) { k_println(str); }

#endif

#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

#include "print.h"
#include "cpu.h"
#include <stdint.h>

typedef enum {
    DEBUG = -1,
    IRQ = 0,
    IRQ2 = 1,
    INFO = 1,
    PROGRESS,
    UNHANDLED,
    FAULT,
    PANIC,
    FOCUS
} Level;

#define MIN_LEVEL PROGRESS
#define CPU_IDS 100

#define __d_printf(level, str, ...) \
if (level >= MIN_LEVEL && CPU__id() <= CPU_IDS) { k_printf("cpu=%i - ", CPU__id()); k_printf(str, __VA_ARGS__); }

#define __d_println(level, str) \
if (level >= MIN_LEVEL && CPU__id() <= CPU_IDS) { k_printf("cpu=%i - ", CPU__id()); k_println(str); }

#endif

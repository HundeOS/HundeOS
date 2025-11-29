#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

#include "print.h"
#include "cpu.h"
#include <stdint.h>

typedef enum {
    DEBUG = -1,
    IRQ = 0,
    IRQ2 = 1,
    INFO = 2,
    MMU,
    PROCESS_DEBUG,
    TIMER_IRQ,
    SCHEDULER_DEBUG,
    MUTEX,
    SYSCALL,
    SCHEDULER,
    PROCESS,
    IODEV,
    IOSUB,
    EXEC,
    FILESYSTEM,
    BROKER,
    PROGRESS,
    UNHANDLED,
    FAULT,
    PANIC,
    FOCUS
} Level;

inline const char * level_to_string(Level level)
{
    switch (level)
    {
        case DEBUG:
            return "DEBUG";
        case IRQ:
            return "IRQ";
        case IRQ2:
            return "IRQ2";
        case INFO:
            return "INFO";
        case PROCESS_DEBUG:
            return "PROCESS_DEBUG";
        case TIMER_IRQ:
            return "TIMER_IRQ";
        case SCHEDULER_DEBUG:
            return "SCHEDULER_DEBUG";
        case SCHEDULER:
            return "SCHEDULER";
        case PROCESS:
            return "PROCESS";
        case PROGRESS:
            return "PROGRESS";
        case FOCUS:
            return "FOCUS";
        case PANIC:
            return "PANIC";
        case UNHANDLED:
            return "UNHANDLED";
        case FAULT:
            return "FAULT";
        case SYSCALL:
            return "SYSCALL";
        case MUTEX:
            return "MUTEX";
        case MMU:
            return "MMU";
        case IOSUB:
            return "IO SUBSYSTEM";
        case IODEV:
            return "IO DEVICE";
        case EXEC:
            return "EXEC";
        case FILESYSTEM:
            return "FILESYSTEM";
        case BROKER:
            return "BROKER";
    }
    return "UNKNOWN";
}

#define MIN_LEVEL PROGRESS
#define CPU_IDS 100

#define __d_printf(level, str, ...) \
if (level >= MIN_LEVEL && CPU__id() <= CPU_IDS) { k_printf("cpu=%i - %s - ", CPU__id(), level_to_string(level)); k_printf(str, __VA_ARGS__); }

#define __d_println(level, str) \
if (level >= MIN_LEVEL && CPU__id() <= CPU_IDS) { k_printf("cpu=%i - %s - ", CPU__id(), level_to_string(level)); k_println(str); }

#endif

#ifndef __KERNEL_DEBUG_H
#define __KERNEL_DEBUG_H

#include "print.h"
#include "cpu.h"
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    DEBUG = 0,
    IRQ,
    IRQ2,
    INFO,
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
    FOCUS,
    LEVEL_COUNT
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
        case LEVEL_COUNT:
            return "UNKNOWN";
    }
    return "UNKNOWN";
}

#define CPU_IDS 100

#define LOG_LEVEL_MASK(level) (1ULL << (level))

#define ENABLED_LOG_LEVELS ( \
    LOG_LEVEL_MASK(PROGRESS) | \
    LOG_LEVEL_MASK(SYSCALL) | \
    LOG_LEVEL_MASK(FAULT) | \
    LOG_LEVEL_MASK(PANIC) \
)

inline bool __d_should_log(Level level)
{
    return level >= 0
        && level < LEVEL_COUNT
        && CPU__id() <= CPU_IDS
        && (ENABLED_LOG_LEVELS & LOG_LEVEL_MASK(level)) != 0;
}

#define __d_printf(level, str, ...) \
if (__d_should_log(level)) { k_printf("cpu=%i - %s - ", CPU__id(), level_to_string(level)); k_printf(str, __VA_ARGS__); }

#define __d_println(level, str) \
if (__d_should_log(level)) { k_printf("cpu=%i - %s - ", CPU__id(), level_to_string(level)); k_println(str); }

#endif

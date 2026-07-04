#ifndef __COMMON_IDLE_DEBUG_H
#define __COMMON_IDLE_DEBUG_H

#include <stdint.h>

#define IDLE_DEBUG_PAGE_VADDR 0x70000000ULL
#define IDLE_DEBUG_MAGIC 0x1D1EDEB600000001ULL

typedef struct IdleDebugState
{
    uint64_t magic;
    uint64_t before_wfi_count;
    uint64_t after_wfi_count;
} IdleDebugState;

typedef IdleDebugState * IdleDebugStatePtr;

#endif

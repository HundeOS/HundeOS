#ifndef __AARCH64_PROCESS_H
#define __AARCH64_PROCESS_H

#include <stdint.h>
#include "../../kernel/kernel.h"

typedef struct ProcessContext {
    uint64_t x[31];  // x0-x30
    uint64_t sp;
    uint64_t pc;     // ELR
    uint64_t pstate; // SPSR
} ProcessContext;

#endif
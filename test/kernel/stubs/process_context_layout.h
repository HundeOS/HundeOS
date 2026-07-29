#ifndef __TEST_KERNEL_STUBS_PROCESS_CONTEXT_LAYOUT_H
#define __TEST_KERNEL_STUBS_PROCESS_CONTEXT_LAYOUT_H

#include <stdint.h>

struct ProcessContext
{
    uint64_t x[31];
    uint64_t sp;
    uint64_t elr;
    uint64_t spsr;
    uint64_t fpcr;
    uint64_t fpsr;
};

#endif

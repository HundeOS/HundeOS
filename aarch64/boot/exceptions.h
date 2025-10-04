#ifndef __AARCH64_BOOT_EXCEPTIONS_H_
#define __AARCH64_BOOT_EXCEPTIONS_H_
#include "boot.h"

__BOOT__(void) boot_panic(const char * msg);
__BOOT__(void) boot_vectors_init();


typedef struct {
    uint64_t x[31];        // x0..x30
    uint64_t sp;           // SP at exception entry (if you save it)
    uint64_t elr_el1;      // Return PC
    uint64_t spsr_el1;     // Saved PSTATE
} boot_trapframe_t;


#endif
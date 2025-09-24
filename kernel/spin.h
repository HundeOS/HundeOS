#ifndef __KERNEL_SPIN_H
#define __KERNEL_SPIN_H
#include <stdint.h>

typedef enum {
    ABORT = -1,
    OK = 0,
} WaitResult;

typedef volatile uint32_t (*spin_check)();
typedef volatile uint32_t (*spin_context_check)(void *);

void spin(uint32_t count);
WaitResult spin_wait(spin_check check);
WaitResult spin_wait_max(spin_check check, uint32_t max);
WaitResult spin_wait_context(spin_context_check check, void * context, uint32_t max);
void wait_for_interrupts();

#endif
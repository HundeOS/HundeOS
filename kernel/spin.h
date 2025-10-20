#ifndef __KERNEL_SPIN_H
#define __KERNEL_SPIN_H
#include <stdint.h>
#include <stdbool.h>

typedef enum {
    WAIT_ABORT = -1,
    WAIT_OK = 0,
} WaitResult;

typedef void * SpinContext;

typedef volatile bool (*SpinCheck)();
typedef volatile bool (*SpinContextCheck)(SpinContext ctx);


void Spin__spin(uint64_t count);
WaitResult Spin__wait(SpinCheck check);
WaitResult Spin__wait_max(SpinCheck check, uint64_t max);
WaitResult Spin__wait_context(SpinContextCheck check, SpinContext context, uint64_t max);

#endif
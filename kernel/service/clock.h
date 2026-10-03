#ifndef HUND_KERNEL_SERVICE_CLOCK_H
#define HUND_KERNEL_SERVICE_CLOCK_H

#include "../../common/time/clock_page.h"
#include "../address.h"
#include "service.h"

typedef struct Process Process;
typedef Process * ProcessPtr;

typedef struct KernelClock
{
    UserAddress mapping;
} KernelClock;

typedef KernelClock * KernelClockPtr;

static inline KernelClock KernelClock__new(void)
{
    return (KernelClock){.mapping = NULL_USER_ADDRESS};
}

Result(ServiceCode) KernelClock__init(KernelClockPtr self, uint64_t timer_frequency,
                                      ConstOptionalRealtimeClockSamplePtr realtime_clock);
Result(ServiceCode) KernelClock__map_process(KernelClockPtr self, ProcessPtr process);

#endif

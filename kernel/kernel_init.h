#ifndef __KERNEL_KERNEL_INIT_H
#define __KERNEL_KERNEL_INIT_H

#include <stdint.h>
#include "types/result.h"
#include "system_info.h"

typedef enum InitStatus
{
    INIT_NOT_STARTED,
    INIT_IN_PROGRESS,
    INIT_DONE,
    INIT_FAILED_AND_PANICKED,
} InitStatus;

typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

ResultDef(InitStatus);

typedef enum SubsystemInitStatuses
{
    SUBSYSTEM_INIT_OK,
    SUBSYSTEM_INIT_FAILED,
} SubsystemInitStatuses;

typedef uint64_t SubsystemInitStatus;

ResultDef(SubsystemInitStatus);

KernelPtr Kernel__instance();
void Kernel__init(KernelPtr self);
InitStatus Kernel__init_status(KernelPtr self);

Result(InitStatus) Kernel__wait_for_init(KernelPtr self);

#endif

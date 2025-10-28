#ifndef __KERNEL_KERNEL_INIT_H
#define __KERNEL_KERNEL_INIT_H

#include "subsystems/io/io_subsystem.h"

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

KernelPtr Kernel__instance();
void Kernel__init(KernelPtr self);
InitStatus Kernel__init_status(KernelPtr self);

Result(InitStatus) Kernel__wait_for_init(KernelPtr self);

#endif
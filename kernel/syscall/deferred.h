#ifndef __KERNEL_SYSCALL_DEFERRED_H
#define __KERNEL_SYSCALL_DEFERRED_H

#include "../types/result.h"
#include "../types/types.h"
#include "../scheduler/parked.h"

Result(Anything) SystemCall__Complete(ParkedKernelContextPtr parked_kernel_context);


#endif
#ifndef __KERNEL_SUBSYSTEMS_MESSAGE_IO_OPERATION_MESSAGE_H
#define __KERNEL_SUBSYSTEMS_MESSAGE_IO_OPERATION_MESSAGE_H

#include "../../kalloc.h"
#include "../../process.h"
#include "../../scheduler/types.h"

typedef struct IOOperationMessage
{
    ProcessID pid;
    OperationKey operation_key;
} IOOperationMessage;

typedef IOOperationMessage * IOOperationMessagePtr;

static inline IOOperationMessage IOOperationMessage__new(ProcessID pid, OperationKey operation_key)
{
    return (IOOperationMessage){
        .pid = pid,
        .operation_key = operation_key,
    };
}

static inline IOOperationMessagePtr IOOperationMessage__heap_new(ProcessID pid, OperationKey operation_key)
{
    IOOperationMessagePtr self = ptr(Kernel__alloc_kernel(sizeof(IOOperationMessage)), IOOperationMessage);
    *self = IOOperationMessage__new(pid, operation_key);
    return self;
}

#endif

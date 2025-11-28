#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

#include <stdint.h>

#include "bin/elf.h"
#include "all_defs.h"
#include "stack.h"
#include "types/comparison.h"

typedef struct ProcessContext ProcessContext;
typedef struct ProcessContext * ProcessContextPtr;

typedef struct ParkedKernelContext ParkedKernelContext;
typedef ParkedKernelContext * ParkedKernelContextPtr;

OptionalDef(ParkedKernelContextPtr);
ResultDef(ParkedKernelContextPtr);

typedef uint64_t ProcessID;
typedef uint64_t ProcessState;
typedef uint64_t Tick;

OptionalDef(ProcessID);
ResultDef(ProcessID);

// state bits
// 6    5      4     3    2         1     0
// dead stop active sleep wait interrupt ready
//
typedef enum ProcessStates{
    PROC_STATE_NEW               = 0b0000000, // Being created
    PROC_STATE_READY             = 0b0000001, // Ready to run
    PROC_STATE_RUNNING           = 0b0010000, // Currently executing
    PROC_STATE_INTERRUPTED       = 0b0000010, // Interrupted by signal
    PROC_STATE_WAITING           = 0b0000100, // Waiting for I/O or event
    PROC_STATE_SLEEPING          = 0b0001000, // Sleeping (timed wait)
    PROC_STATE_STOPPED           = 0b0100000, // Stopped (e.g., by signal)
    PROC_STATE_DEAD              = 0b1000000, // Being destroyed
    PROC_STATE_SWITCHING_CONTEXT = 0b0100100, // Switching context
} ProcessStates;

typedef enum ProcessErrors
{
    PROC_ERROR_NO_PARKED = -1,
} ProcessErrors;

inline const char * ProcessState__to_string(ProcessState state)
{
    switch (state)
    {
        case PROC_STATE_NEW:
            return "PROC_STATE_NEW";
        case PROC_STATE_READY:
            return "PROC_STATE_READY";
        case PROC_STATE_RUNNING:
            return "PROC_STATE_RUNNING";
        case PROC_STATE_INTERRUPTED:
            return "PROC_STATE_INTERRUPTED";
        case PROC_STATE_WAITING:
            return "PROC_STATE_WAITING";
        case PROC_STATE_SLEEPING:
            return "PROC_STATE_SLEEPING";
        case PROC_STATE_STOPPED:
            return "PROC_STATE_STOPPED";
        case PROC_STATE_DEAD:
            return "PROC_STATE_DEAD";
        case PROC_STATE_SWITCHING_CONTEXT:
            return "PROC_STATE_SWITCHING_CONTEXT";
        default:
            return "UNKNOWN";
    }
}

typedef enum ProcessSetupStatus
{
    PROCESS_FAILED_TO_SETUP = -1,
    PROCESS_SETUP = 0,
} ProcessSetupStatus;

typedef struct Process Process;
typedef Process * ProcessPtr;

OptionalDef(ProcessPtr);
ResultDef(ProcessPtr);

typedef struct Process {
    KernelAddress page_table;

    ProcessContextPtr context;
    Stack stack;
    Optional(ProcessPtr) parent;
    ProcessID pid;
    ProcessState state;
    UserAddress entry_point;
    Tick last_tick;
    KString name;

    Optional(ParkedKernelContextPtr) parked_context;
} Process;

ProcessPtr Process__new(
    KString name, KernelAddress page_table, UserAddress entry, Stack stack, Optional(ProcessPtr) parent
);

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessTable, &Ptr__equals);

OptionalType(ProcessTable *, ProcessTablePtr);

Result(ProcessPtr) Process__setup(Elf binary, Optional(ProcessPtr) parent);

#endif
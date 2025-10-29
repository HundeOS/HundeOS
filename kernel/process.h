#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

#include <stdint.h>

#include "arch/page_table.h"
#include "bin/elf.h"
#include "all_defs.h"
#include "stack.h"
#include "subsystems/io/io.h"
#include "types/comparison.h"


FixedArrayDefine(IOHandle, MAX_HANDLES_PER_PROCESS, ProcessHandles, SimpleEquals(uint64_t));

typedef struct ProcessContext ProcessContext;

typedef uint64_t ProcessID;

typedef enum {
    PROC_STATE_NEW,         // Being created
    PROC_STATE_READY,       // Ready to run
    PROC_STATE_RUNNING,     // Currently executing
    PROC_STATE_WAITING,     // Waiting for I/O or event
    PROC_STATE_SLEEPING,    // Sleeping (timed wait)
    PROC_STATE_STOPPED,     // Stopped (e.g., by signal)
    PROC_STATE_ZOMBIE,      // Terminated but not reaped
    PROC_STATE_DEAD,         // Being destroyed
    PROC_STATE_UNALLOCATED         // Being destroyed
} ProcessState;

typedef struct Process {
    KernelAddress page_table;

    ProcessContext * context;
    Stack stack;

    ProcessID pid;
    ProcessState state;
    UserAddress entry_point;
    ProcessHandles handles;
} Process;

typedef Process * ProcessPtr;

ProcessPtr Process__spawn_from_elf(Elf * elf);
ProcessPtr Process__new(KernelAddress page_table, UserAddress entry, Stack stack);

OptionalDef(ProcessPtr);

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessTable, &Ptr__equals);
FixedArrayDefine(ProcessPtr, MAX_CPUS, CPUProcesses, &Ptr__equals);

OptionalType(ProcessTable *, ProcessTablePtr);
SimpleEqualsPreDef(ProcessTablePtr);

FixedArrayDefine(ProcessTablePtr, MAX_CPUS, CPUProcessTables, &Ptr__equals);

Optional(ProcessPtr) Process__current_for_cpu(int cpu);
Optional(ProcessTablePtr) Process__all_for_cpu(int cpu);

#endif
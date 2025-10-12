#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

#include <stdint.h>

#include "arch/page_table.h"
#include "bin/elf.h"
#include "all_defs.h"
#include "stack.h"

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
} Process;

Process * Process__spawn_from_elf(Elf * elf);
Process * Process__new(KernelAddress page_table, UserAddress entry, Stack stack);

typedef Process * ProcessPtr;

FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessTable);
FixedArrayDefine(ProcessTable, MAX_CPUS, CPUProcesses);

#endif
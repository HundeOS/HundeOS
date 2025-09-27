#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H
#include "mem.h"
// architecture specific
typedef struct ProcessContext ProcessContext;


typedef uint64_t ProcessId;

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

typedef void (*ProcessEntryPoint)();

typedef struct Process {
    // 1. Separate memory space (page tables)
    PageTable page_table;     // Each process gets its own L1 table

    // 2. Saved CPU state
    void* context; // Registers saved on context switch

    // 3. Stack
    Stack stack;       // Each process has its own stack

    // 4. Process info
    ProcessId pid;
    ProcessState state;
    ProcessEntryPoint entry_point;
} Process;

ProcessId Process__spawn(ProcessEntryPoint entry);

#endif
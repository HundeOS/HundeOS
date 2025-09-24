#ifndef __KERNEL_PROCESS_H
#define __KERNEL_PROCESS_H

// architecture specific
typedef struct process_context process_context;
typedef struct process process;

typedef enum {
    PROC_STATE_NEW,         // Being created
    PROC_STATE_READY,       // Ready to run
    PROC_STATE_RUNNING,     // Currently executing
    PROC_STATE_WAITING,     // Waiting for I/O or event
    PROC_STATE_SLEEPING,    // Sleeping (timed wait)
    PROC_STATE_STOPPED,     // Stopped (e.g., by signal)
    PROC_STATE_ZOMBIE,      // Terminated but not reaped
    PROC_STATE_DEAD         // Being destroyed
} process_state;

#endif
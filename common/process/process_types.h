#ifndef HUND_PROCESS_TYPES_H
#define HUND_PROCESS_TYPES_H
#include <stdint.h>
#include "../types/bstring.h"
#include "../types/optional.h"
typedef uint64_t ProcessState;
typedef ProcessState * ProcessStatePtr;

// state bits
// 6    5      4     3    2         1     0
// dead stop active sleep wait interrupt ready
//
typedef enum ProcessStates
{
    PROC_STATE_NEW = 0b0000000,               // Being created
    PROC_STATE_READY = 0b0000001,             // Ready to run
    PROC_STATE_RUNNING = 0b0010000,           // Currently executing
    PROC_STATE_INTERRUPTED = 0b0000010,       // Interrupted by signal
    PROC_STATE_WAITING = 0b0000100,           // Waiting for I/O or event
    PROC_STATE_SLEEPING = 0b0001000,          // Sleeping (timed wait)
    PROC_STATE_STOPPED = 0b0100000,           // Stopped (e.g., by signal)
    PROC_STATE_DEAD = 0b1000000,              // Being destroyed
    PROC_STATE_SWITCHING_CONTEXT = 0b0100100, // Switching context
} ProcessStates;

typedef uint64_t ParkReason;

typedef enum ParkReasons
{
    PARK_WAIT_CHILD = 1,
    PARK_WAIT_IO = 2,
    PARK_WAIT_SERVICE_EVENT = 3,
    PARK_WAIT_CHILD_EVENT = 4,
    PARK_WAIT_SERVICE_READY = 5,
    PARK_SLEEP = 6,
    PARK_WAIT_QUEUE = 7,
    PARK_WAIT_EXEC_LOAD = 8,
    PARK_WAIT_SIGNAL = 9,
} ParkReasons;

OptionalDef(ParkReason);

typedef enum ProcessClass
{
    PROCESS_CLASS_NORMAL = 0,
    PROCESS_CLASS_SERVICE = 1,
    PROCESS_CLASS_REALTIME_SERVICE = 2,
} ProcessClass;

static inline const char * ProcessState__to_string(ProcessState state)
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

BString ParkReason__to_string(ParkReason reason);
#endif

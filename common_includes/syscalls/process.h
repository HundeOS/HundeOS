#ifndef _COMMON_INCLUDES_SYSCALLS_PROCESS_H
#define _COMMON_INCLUDES_SYSCALLS_PROCESS_H

#include <stdint.h>
#include "types/result.h"

#define PROCESS_INFO_NAME_SIZE 64
#define PROCESS_INFO_STATE_SIZE 32
#define PROCESS_INFO_CLASS_SIZE 16
#define PROCESS_INFO_WAIT_SIZE 32
#define PROCESS_INFO_USER_SIZE 16

typedef int64_t ProcessListResult;
typedef int64_t ProcessSignalResult;
typedef int64_t ProcessChildEventResult;
typedef int64_t ProcessSleepResult;
typedef int64_t ProcessTimesResult;
typedef int64_t ProcessIdentityResult;
typedef int64_t ProcessForkResult;
typedef int64_t ProcessGroupResult;

typedef uint64_t ProcessSignal;
typedef ProcessSignal * ProcessSignalPtr;
typedef uint64_t ProcessSignalMask;
typedef uint64_t UserID;
typedef uint64_t GroupID;
typedef uint64_t ProcessGroupID;
typedef uint64_t ProcessSessionID;
typedef uint64_t ProcessCapabilities;

typedef struct ProcessIdentity
{
    uint64_t pid;
    uint64_t parent_pid;
    ProcessGroupID process_group_id;
    ProcessSessionID session_id;
} ProcessIdentity;

typedef ProcessIdentity * ProcessIdentityPtr;
ResultDef(ProcessIdentity);

static inline ProcessIdentity ProcessIdentity__new(uint64_t pid, uint64_t parent_pid)
{
    return (ProcessIdentity){
        .pid = pid,
        .parent_pid = parent_pid,
        .process_group_id = 0,
        .session_id = 0,
    };
}

typedef enum ProcessIdentityStatus
{
    PROCESS_IDENTITY_OK = 0,
    PROCESS_IDENTITY_INVALID_ARGUMENT = -1,
} ProcessIdentityStatus;

typedef enum ProcessForkStatus
{
    PROCESS_FORK_INVALID_CALLER = -1,
    PROCESS_FORK_NO_MEMORY = -2,
    PROCESS_FORK_UNSUPPORTED_STATE = -3,
    PROCESS_FORK_SCHEDULE_FAILED = -4,
    PROCESS_FORK_INVALID_OPERATION = -5,
    PROCESS_FORK_ALREADY_PENDING = -6,
    PROCESS_FORK_CHILD_NOT_FOUND = -7,
} ProcessForkStatus;

typedef enum ProcessForkOperation
{
    PROCESS_FORK_PREPARE = 1,
    PROCESS_FORK_RESUME = 2,
    PROCESS_FORK_ABORT = 3,
} ProcessForkOperation;

typedef enum ProcessGroupOperations
{
    PROCESS_GROUP_GET = 1,
    PROCESS_GROUP_SET = 2,
    PROCESS_GROUP_CREATE_SESSION = 3,
} ProcessGroupOperations;

typedef struct ProcessGroupRequest
{
    uint64_t operation;
    int64_t pid;
    int64_t process_group_id;
    int64_t result_id;
} ProcessGroupRequest;

typedef ProcessGroupRequest * ProcessGroupRequestPtr;

typedef enum ProcessGroupResults
{
    PROCESS_GROUP_OK = 0,
    PROCESS_GROUP_INVALID_ARGUMENT = -1,
    PROCESS_GROUP_PROCESS_NOT_FOUND = -2,
    PROCESS_GROUP_PERMISSION_DENIED = -3,
} ProcessGroupResults;

typedef struct ProcessCredentials
{
    UserID user_id;
    GroupID group_id;
} ProcessCredentials;

typedef ProcessCredentials * ProcessCredentialsPtr;
typedef const ProcessCredentials * ConstProcessCredentialsPtr;
ResultDef(ProcessCredentials);

static inline ProcessCredentials ProcessCredentials__new(UserID user_id, GroupID group_id)
{
    return (ProcessCredentials){.user_id = user_id, .group_id = group_id};
}

typedef enum ProcessCredentialsStatus
{
    PROCESS_CREDENTIALS_OK = 0,
    PROCESS_CREDENTIALS_INVALID_ARGUMENT = -1,
    PROCESS_CREDENTIALS_NOT_FOUND = -2,
} ProcessCredentialsStatus;

typedef enum ProcessBuiltinUsers
{
    PROCESS_USER_ROOT = 0,
    PROCESS_USER_HUND = 1,
    PROCESS_USER_USER = 2,
} ProcessBuiltinUsers;

typedef enum ProcessBuiltinGroups
{
    PROCESS_GROUP_ROOT = 0,
    PROCESS_GROUP_HUND = 1,
    PROCESS_GROUP_USER = 2,
} ProcessBuiltinGroups;

typedef enum ProcessCapabilityBits
{
    PROCESS_CAP_NONE = 0,
    PROCESS_CAP_BECOME_SERVICE = 1ULL << 0,
    PROCESS_CAP_MAP_MMIO = 1ULL << 1,
    PROCESS_CAP_SUBSCRIBE_IRQ = 1ULL << 2,
    PROCESS_CAP_REGISTER_BLOCK_DEVICE = 1ULL << 3,
    PROCESS_CAP_HANDLE_BLOCK_REQUESTS = 1ULL << 4,
    PROCESS_CAP_MOUNT_FILESYSTEM = 1ULL << 5,
    PROCESS_CAP_USE_BLOCK_DEVICES = 1ULL << 6,
    PROCESS_CAP_MANAGE_SHARED_PAGES = 1ULL << 7,
    PROCESS_CAP_ALL = UINT64_MAX,
} ProcessCapabilityBits;

typedef enum ProcessSignals
{
    PROCESS_SIGNAL_HANGUP = 1,
    PROCESS_SIGNAL_INTERRUPT = 2,
    PROCESS_SIGNAL_QUIT = 3,
    PROCESS_SIGNAL_ILLEGAL_INSTRUCTION = 4,
    PROCESS_SIGNAL_TRAP = 5,
    PROCESS_SIGNAL_ABORT = 6,
    PROCESS_SIGNAL_EMULATION = 7,
    PROCESS_SIGNAL_FLOATING_POINT = 8,
    PROCESS_SIGNAL_KILL = 9,
    PROCESS_SIGNAL_BUS = 10,
    PROCESS_SIGNAL_SEGMENTATION = 11,
    PROCESS_SIGNAL_BAD_SYSTEM_CALL = 12,
    PROCESS_SIGNAL_PIPE = 13,
    PROCESS_SIGNAL_ALARM = 14,
    PROCESS_SIGNAL_TERMINATE = 15,
    PROCESS_SIGNAL_URGENT = 16,
    PROCESS_SIGNAL_STOP = 17,
    PROCESS_SIGNAL_TERMINAL_STOP = 18,
    PROCESS_SIGNAL_CONTINUE = 19,
    PROCESS_SIGNAL_CHILD = 20,
    PROCESS_SIGNAL_TERMINAL_INPUT = 21,
    PROCESS_SIGNAL_TERMINAL_OUTPUT = 22,
    PROCESS_SIGNAL_IO = 23,
    PROCESS_SIGNAL_CPU_LIMIT = 24,
    PROCESS_SIGNAL_FILE_LIMIT = 25,
    PROCESS_SIGNAL_VIRTUAL_ALARM = 26,
    PROCESS_SIGNAL_PROFILE = 27,
    PROCESS_SIGNAL_WINDOW_CHANGE = 28,
    PROCESS_SIGNAL_INFO = 29,
    PROCESS_SIGNAL_USER_1 = 30,
    PROCESS_SIGNAL_USER_2 = 31,
    PROCESS_SIGNAL_THREAD = 32,
} ProcessSignals;

#define PROCESS_SIGNAL_COUNT 32

typedef uint64_t ProcessSignalHandler;

#define PROCESS_SIGNAL_DEFAULT_HANDLER 0ULL
#define PROCESS_SIGNAL_IGNORE_HANDLER 1ULL

typedef struct ProcessSignalAction
{
    ProcessSignalHandler handler;
    ProcessSignalHandler restorer;
    ProcessSignalMask mask;
    uint64_t flags;
} ProcessSignalAction;

typedef ProcessSignalAction * ProcessSignalActionPtr;
typedef const ProcessSignalAction * ConstProcessSignalActionPtr;

static inline ProcessSignalAction ProcessSignalAction__new(uint64_t handler, uint64_t restorer, ProcessSignalMask mask,
                                                           uint64_t flags)
{
    return (ProcessSignalAction){
        .handler = handler,
        .restorer = restorer,
        .mask = mask,
        .flags = flags,
    };
}

#define PROCESS_SIGNAL_ACTION_ONSTACK 0x0001ULL
#define PROCESS_SIGNAL_ACTION_RESTART 0x0002ULL
#define PROCESS_SIGNAL_ACTION_RESETHAND 0x0004ULL
#define PROCESS_SIGNAL_ACTION_NOCLDSTOP 0x0008ULL
#define PROCESS_SIGNAL_ACTION_NODEFER 0x0010ULL
#define PROCESS_SIGNAL_ACTION_NOCLDWAIT 0x0020ULL

typedef enum ProcessSignalControlOperations
{
    PROCESS_SIGNAL_CONTROL_ACTION = 1,
    PROCESS_SIGNAL_CONTROL_MASK = 2,
    PROCESS_SIGNAL_CONTROL_PENDING = 3,
    PROCESS_SIGNAL_CONTROL_SUSPEND = 4,
    PROCESS_SIGNAL_CONTROL_ALARM = 5,
} ProcessSignalControlOperations;

typedef enum ProcessSignalMaskOperations
{
    PROCESS_SIGNAL_MASK_BLOCK = 1,
    PROCESS_SIGNAL_MASK_UNBLOCK = 2,
    PROCESS_SIGNAL_MASK_SET = 3,
} ProcessSignalMaskOperations;

typedef struct ProcessSignalControlRequest
{
    uint64_t operation;
    ProcessSignal signal;
    uint64_t replace;
    ProcessSignalAction action;
    ProcessSignalAction previous_action;
    uint64_t mask_operation;
    ProcessSignalMask mask;
    ProcessSignalMask previous_mask;
    uint64_t alarm_seconds;
    uint64_t previous_alarm_seconds;
} ProcessSignalControlRequest;

typedef ProcessSignalControlRequest * ProcessSignalControlRequestPtr;

typedef enum ProcessListResults
{
    PROCESS_LIST_INVALID_ARGUMENT = -1,
} ProcessListResults;

typedef enum ProcessSignalResults
{
    PROCESS_SIGNAL_OK = 0,
    PROCESS_SIGNAL_INVALID_ARGUMENT = -1,
    PROCESS_SIGNAL_PROCESS_NOT_FOUND = -2,
    PROCESS_SIGNAL_UNSUPPORTED = -3,
    PROCESS_SIGNAL_NO_MEMORY = -4,
    PROCESS_SIGNAL_NO_FRAME = -5,
    PROCESS_SIGNAL_PERMISSION_DENIED = -6,
    PROCESS_SIGNAL_INTERRUPTED = -7,
} ProcessSignalResults;

typedef enum ProcessChildEventType
{
    PROCESS_CHILD_EVENT_EXITED = 1,
    PROCESS_CHILD_EVENT_STOPPED = 2,
    PROCESS_CHILD_EVENT_CONTINUED = 3,
} ProcessChildEventType;

typedef struct ProcessChildEvent
{
    uint64_t pid;
    ProcessGroupID process_group_id;
    int64_t status;
    uint64_t type;
    uint64_t user_usec;
    uint64_t system_usec;
} ProcessChildEvent;

typedef ProcessChildEvent * ProcessChildEventPtr;
typedef const ProcessChildEvent * ConstProcessChildEventPtr;

static inline ProcessChildEvent ProcessChildEvent__new(void)
{
    return (ProcessChildEvent){
        .pid = 0,
        .process_group_id = 0,
        .status = 0,
        .type = 0,
        .user_usec = 0,
        .system_usec = 0,
    };
}

typedef enum ProcessChildEventResults
{
    PROCESS_CHILD_EVENT_OK = 0,
    PROCESS_CHILD_EVENT_PENDING = 1,
    PROCESS_CHILD_EVENT_INVALID_ARGUMENT = -1,
    PROCESS_CHILD_EVENT_QUEUE_EMPTY = -2,
    PROCESS_CHILD_EVENT_NO_CHILD = -3,
} ProcessChildEventResults;

typedef enum ProcessChildWaitOptions
{
    PROCESS_CHILD_WAIT_NOHANG = 1U << 0,
    PROCESS_CHILD_WAIT_UNTRACED = 1U << 1,
    PROCESS_CHILD_WAIT_CONTINUED = 1U << 3,
} ProcessChildWaitOptions;

#define PROCESS_WAIT_STATUS_EXITED(code) ((((uint64_t)(code)) & 0xffU) << 8U)
#define PROCESS_WAIT_STATUS_SIGNALED(signal) (((uint64_t)(signal)) & 0x7fU)
#define PROCESS_WAIT_STATUS_STOPPED(signal) (0x7fU | ((((uint64_t)(signal)) & 0xffU) << 8U))
#define PROCESS_WAIT_STATUS_CONTINUED 0xffffU
#define PROCESS_WAIT_ABORT_SIGNAL 6U

typedef enum ProcessSleepResults
{
    PROCESS_SLEEP_OK = 0,
    PROCESS_SLEEP_INVALID_ARGUMENT = -1,
    PROCESS_SLEEP_FAILED = -2,
} ProcessSleepResults;

typedef enum ProcessTimesResults
{
    PROCESS_TIMES_OK = 0,
    PROCESS_TIMES_INVALID_ARGUMENT = -1,
} ProcessTimesResults;

typedef struct ProcessTimes
{
    uint64_t elapsed_usec;
    uint64_t user_usec;
    uint64_t system_usec;
    uint64_t children_user_usec;
    uint64_t children_system_usec;
} ProcessTimes;

typedef ProcessTimes * ProcessTimesPtr;

static inline ProcessTimes ProcessTimes__new(void)
{
    return (ProcessTimes){0};
}

#define PROCESS_CPU_TICK_USEC 1000ULL
#define PROCESS_CPU_SHORT_TICKS 100ULL
#define PROCESS_CPU_LONG_TICKS (10 * PROCESS_CPU_SHORT_TICKS)

typedef struct ProcessCPUUsage
{
    uint64_t total_usec;
    uint64_t user_usec;
    uint64_t system_usec;
    uint64_t short_basis_points;
    uint64_t long_basis_points;
    uint64_t scheduled_usec;
    uint64_t scheduled_short_basis_points;
    uint64_t scheduled_long_basis_points;
} ProcessCPUUsage;

static inline ProcessCPUUsage ProcessCPUUsage__new(void)
{
    return (ProcessCPUUsage){0};
}

typedef struct ProcessInfo
{
    uint64_t pid;
    uint64_t cpu;
    ProcessCPUUsage cpu_usage;
    uint64_t user_wait_usec;
    char name[PROCESS_INFO_NAME_SIZE];
    char state[PROCESS_INFO_STATE_SIZE];
    char wait_reason[PROCESS_INFO_WAIT_SIZE];
    char class_name[PROCESS_INFO_CLASS_SIZE];
    char user_name[PROCESS_INFO_USER_SIZE];
} ProcessInfo;

typedef ProcessInfo * ProcessInfoPtr;

static inline ProcessInfo ProcessInfo__new(uint64_t pid)
{
    return (ProcessInfo){
        .pid = pid,
    };
}

typedef struct ProcessListRequest
{
    ProcessInfoPtr entries;
    uint64_t max_entries;
} ProcessListRequest;

typedef ProcessListRequest * ProcessListRequestPtr;

static inline ProcessListRequest ProcessListRequest__new(ProcessInfoPtr entries, uint64_t max_entries)
{
    return (ProcessListRequest){
        .entries = entries,
        .max_entries = max_entries,
    };
}

#endif

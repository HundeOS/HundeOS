#ifndef _COMMON_INCLUDES_SYSCALLS_PROCESS_H
#define _COMMON_INCLUDES_SYSCALLS_PROCESS_H

#include <stdint.h>

#define PROCESS_INFO_NAME_SIZE 64
#define PROCESS_INFO_STATE_SIZE 32
#define PROCESS_INFO_CLASS_SIZE 16
#define PROCESS_INFO_WAIT_SIZE 32
#define PROCESS_INFO_USER_SIZE 16

typedef int64_t ProcessListResult;
typedef int64_t ProcessSignalResult;
typedef int64_t ProcessChildEventResult;
typedef int64_t ProcessSleepResult;

typedef uint64_t ProcessSignal;
typedef uint64_t ProcessSignalMask;
typedef uint64_t UserID;
typedef uint64_t GroupID;
typedef uint64_t ProcessCapabilities;

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
    PROCESS_SIGNAL_INTERRUPT = 2,
    PROCESS_SIGNAL_KILL = 9,
    PROCESS_SIGNAL_TERMINATE = 15,
} ProcessSignals;

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
} ProcessSignalResults;

typedef enum ProcessChildEventType
{
    PROCESS_CHILD_EVENT_EXITED = 1,
} ProcessChildEventType;

typedef struct ProcessChildEvent
{
    uint64_t pid;
    int64_t status;
    uint64_t type;
} ProcessChildEvent;

typedef ProcessChildEvent * ProcessChildEventPtr;
typedef const ProcessChildEvent * ConstProcessChildEventPtr;

static inline ProcessChildEvent ProcessChildEvent__new(void)
{
    return (ProcessChildEvent){
        .pid = 0,
        .status = 0,
        .type = 0,
    };
}

typedef enum ProcessChildEventResults
{
    PROCESS_CHILD_EVENT_OK = 0,
    PROCESS_CHILD_EVENT_INVALID_ARGUMENT = -1,
    PROCESS_CHILD_EVENT_QUEUE_EMPTY = -2,
} ProcessChildEventResults;

typedef enum ProcessSleepResults
{
    PROCESS_SLEEP_OK = 0,
    PROCESS_SLEEP_INVALID_ARGUMENT = -1,
    PROCESS_SLEEP_FAILED = -2,
} ProcessSleepResults;

typedef struct ProcessInfo
{
    uint64_t pid;
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

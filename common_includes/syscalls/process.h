#ifndef _COMMON_INCLUDES_SYSCALLS_PROCESS_H
#define _COMMON_INCLUDES_SYSCALLS_PROCESS_H

#include <stdint.h>

#define PROCESS_INFO_NAME_SIZE 64
#define PROCESS_INFO_STATE_SIZE 32
#define PROCESS_INFO_CLASS_SIZE 16
#define PROCESS_INFO_WAIT_SIZE 32

typedef int64_t ProcessListResult;

typedef enum ProcessListResults
{
    PROCESS_LIST_INVALID_ARGUMENT = -1,
} ProcessListResults;

typedef struct ProcessInfo
{
    uint64_t pid;
    char name[PROCESS_INFO_NAME_SIZE];
    char state[PROCESS_INFO_STATE_SIZE];
    char wait_reason[PROCESS_INFO_WAIT_SIZE];
    char class_name[PROCESS_INFO_CLASS_SIZE];
}
ProcessInfo;

typedef ProcessInfo * ProcessInfoPtr;

typedef struct ProcessListRequest
{
    ProcessInfoPtr entries;
    uint64_t max_entries;
}
ProcessListRequest;

typedef ProcessListRequest * ProcessListRequestPtr;

#endif

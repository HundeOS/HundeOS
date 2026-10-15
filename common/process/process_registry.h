#ifndef HUND_PROCESS_REGISTRY_H
#define HUND_PROCESS_REGISTRY_H
#include "process_types.h"
#include "../ipc/io_diagnostics.h"
#include "../types/atomic_record_array.h"
#include "syscalls/process.h"

#define PROCESS_REGISTRY_VADDR 0x78000000ULL
#define PROCESS_REGISTRY_CPUS 16U
#define PROCESS_REGISTRY_CPU_CAPACITY 2049U
#define PROCESS_REGISTRY_CAPACITY (PROCESS_REGISTRY_CPUS * PROCESS_REGISTRY_CPU_CAPACITY)
#define PROCESS_REGISTRY_VERSION 8ULL

typedef struct ProcessUserWait
{
    uint64_t started_at_usec;
    uint64_t elapsed_usec;
} ProcessUserWait;

OptionalDef(ProcessUserWait);

typedef struct ProcessQueueWaitUsage
{
    uint64_t parks;
    uint64_t activity_wakes;
    uint64_t timeout_wakes;
    uint64_t selections;
    uint64_t speculative_selections;
    uint64_t parked_usec;
    uint64_t max_parked_usec;
    uint64_t runnable_usec;
    uint64_t max_runnable_usec;
} ProcessQueueWaitUsage;

typedef struct ProcessMemoryUsage
{
    uint64_t private_bytes;
    uint64_t shared_bytes;
    uint64_t device_bytes;
} ProcessMemoryUsage;

OptionalDef(ProcessMemoryUsage);

static inline ProcessMemoryUsage ProcessMemoryUsage__new(void)
{
    return (ProcessMemoryUsage){.private_bytes = 0, .shared_bytes = 0, .device_bytes = 0};
}

typedef struct ProcessRecord
{
    uint64_t pid;
    uint64_t cpu;
    uint64_t sampled_at_usec;
    BString name;
    ProcessState state;
    Optional(ParkReason) wait_reason;
    Optional(ProcessUserWait) user_wait;
    uint64_t user_wait_usec;
    ProcessClass process_class;
    ProcessCredentials credentials;
    ProcessCPUUsage cpu_usage;
    ProcessQueueWaitUsage queue_wait_usage;
    Optional(IODiagnosticsSnapshot) io_diagnostics;
    Optional(ProcessMemoryUsage) memory;
} ProcessRecord;

typedef ProcessRecord * ProcessRecordPtr;
typedef const ProcessRecord * ConstProcessRecordPtr;
ResultDef(ProcessRecord);
OptionalDef(ProcessRecord);
typedef Optional(ProcessRecord) OptionalProcessRecord;
ResultDef(OptionalProcessRecord);

AtomicRecordArrayDefine(ProcessRecord, PROCESS_REGISTRY_CAPACITY, ProcessRecords);
AtomicFixedArrayDefine(uint64_t, PROCESS_REGISTRY_CPUS, ProcessRecordCounts);

typedef struct ProcessRegistry
{
    uint64_t version;
    ProcessRecordCounts counts;
    ProcessRecords records;
} ProcessRegistry;

typedef ProcessRegistry * ProcessRegistryPtr;
typedef const ProcessRegistry * ConstProcessRegistryPtr;
ResultDef(ConstProcessRegistryPtr);
#define PROCESS_REGISTRY_SIZE (((sizeof(ProcessRegistry) + 4095ULL) / 4096ULL) * 4096ULL)
_Static_assert(PROCESS_REGISTRY_VADDR + PROCESS_REGISTRY_SIZE <= 0x80000000ULL, "Process registry region overflow");

Result(OptionalProcessRecord) ProcessRegistry__at(ConstProcessRegistryPtr self, uint64_t index);
Result(OptionalProcessRecord) ProcessRegistry__find(ConstProcessRegistryPtr self, uint64_t pid);
Result(CollectionCode)
    ProcessRegistry__set(ProcessRegistryPtr self, uint64_t cpu, uint64_t index, ConstProcessRecordPtr record);
Result(CollectionCode) ProcessRegistry__set_count(ProcessRegistryPtr self, uint64_t cpu, uint64_t count);
ProcessInfo ProcessRecord__info(ConstProcessRecordPtr self);
#endif

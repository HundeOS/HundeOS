#ifndef HUNDE_PROCESS_SNAPSHOT_H
#define HUNDE_PROCESS_SNAPSHOT_H

#include "host_component.h"

typedef struct HundeCpuUsage
{
    uint64_t total_usec;
    uint64_t user_usec;
    uint64_t system_usec;
    uint64_t short_basis_points;
    uint64_t long_basis_points;
    uint64_t scheduled_usec;
    uint64_t scheduled_short_basis_points;
    uint64_t scheduled_long_basis_points;
} HundeCpuUsage;

typedef struct HundeQueueWait
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
} HundeQueueWait;

typedef struct HundeMemoryUsage
{
    uint64_t private_bytes;
    uint64_t shared_bytes;
    uint64_t device_bytes;
} HundeMemoryUsage;

typedef struct HundeLatency
{
    uint64_t p50_usec;
    uint64_t p70_usec;
    uint64_t p90_usec;
    uint64_t p99_usec;
} HundeLatency;

typedef struct HundeDiagnostics
{
    uint64_t uart_bytes;
    uint64_t uart_irqs;
    uint64_t uart_reads;
    uint64_t uart_empty;
    uint64_t backend_reads;
    uint64_t backend_empty;
    uint64_t wait_entries;
    uint64_t unchanged_wakes;
    uint64_t block_cache_hits;
    uint64_t block_cache_misses;
    uint64_t block_cache_evictions;
    uint64_t inode_cache_hits;
    uint64_t inode_cache_misses;
    uint64_t inode_cache_evictions;
    uint64_t block_submissions;
    uint64_t block_blocks;
    uint64_t block_failures;
    uint64_t block_segmented_submissions;
    HundeLatency block_device;
    HundeLatency block_device_irq;
    HundeLatency block_manager_wake;
    HundeLatency block_irq_relay;
    HundeLatency block_fiber_wake;
    HundeLatency block_completion;
} HundeDiagnostics;

typedef struct HundeProcessText
{
    uint64_t length;
    uint8_t bytes[1024];
} HundeProcessText;

typedef struct HundeProcessSnapshot
{
    uint64_t pid;
    uint64_t cpu;
    uint64_t sampled_at_usec;
    uint64_t user_id;
    uint64_t group_id;
    uint64_t user_wait_usec;
    uint64_t has_memory;
    uint64_t has_diagnostics;
    HundeCpuUsage cpu_usage;
    HundeQueueWait queue_wait;
    HundeMemoryUsage memory;
    HundeDiagnostics diagnostics;
    HundeProcessText name;
    HundeProcessText state;
    HundeProcessText wait;
    HundeProcessText class_name;
} HundeProcessSnapshot;

typedef struct HundeProcessResult
{
    HundeAbiResultKind kind;
    uint32_t found;
    HundeProcessSnapshot snapshot;
    HundeAbiError error;
} HundeProcessResult;

typedef HundeProcessResult (*HundeProcessLookup)(uint64_t selector, uint32_t by_index);

typedef struct HundeTraceResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    uint64_t size;
    HundeAbiError error;
} HundeTraceResult;

typedef HundeTraceResult (*HundeTraceLoad)(uint8_t * destination, uint64_t capacity);

_Static_assert(sizeof(HundeProcessText) == 1032, "Process text ABI changed");
_Static_assert(sizeof(HundeProcessSnapshot) == 4688, "Process snapshot ABI changed");
_Static_assert(offsetof(HundeProcessSnapshot, cpu_usage) == 64, "Process CPU ABI changed");
_Static_assert(offsetof(HundeProcessSnapshot, diagnostics) == 224, "Process diagnostics ABI changed");
_Static_assert(offsetof(HundeProcessSnapshot, name) == 560, "Process name ABI changed");
_Static_assert(offsetof(HundeProcessSnapshot, class_name) == 3656, "Process class ABI changed");
_Static_assert(sizeof(HundeProcessResult) == 4720, "Process result ABI changed");
_Static_assert(sizeof(HundeTraceResult) == 40, "Trace result ABI changed");
#endif

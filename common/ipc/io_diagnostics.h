#ifndef HUND_IO_DIAGNOSTICS_H
#define HUND_IO_DIAGNOSTICS_H
#include "../types/optional.h"
#include <stdbool.h>
#include <stdint.h>

#define IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT 128U
#define IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT 4U

typedef enum IODiagnosticEvent
{
    IO_DIAGNOSTIC_UART_BYTES,
    IO_DIAGNOSTIC_UART_IRQS,
    IO_DIAGNOSTIC_UART_READS,
    IO_DIAGNOSTIC_UART_EMPTY,
    IO_DIAGNOSTIC_BACKEND_READS,
    IO_DIAGNOSTIC_BACKEND_EMPTY,
    IO_DIAGNOSTIC_WARDEN_RELAYS,
    IO_DIAGNOSTIC_WAIT_ENTRIES,
    IO_DIAGNOSTIC_UNCHANGED_WAKES,
    IO_DIAGNOSTIC_BLOCK_CACHE_HITS,
    IO_DIAGNOSTIC_BLOCK_CACHE_MISSES,
    IO_DIAGNOSTIC_BLOCK_CACHE_EVICTIONS,
    IO_DIAGNOSTIC_INODE_CACHE_HITS,
    IO_DIAGNOSTIC_INODE_CACHE_MISSES,
    IO_DIAGNOSTIC_INODE_CACHE_EVICTIONS,
} IODiagnosticEvent;

typedef struct IODiagnosticLatency
{
    uint64_t buckets[IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT];
} IODiagnosticLatency;

typedef IODiagnosticLatency * IODiagnosticLatencyPtr;
typedef const IODiagnosticLatency * ConstIODiagnosticLatencyPtr;

typedef struct IODiagnosticLatencyPercentiles
{
    uint64_t p50_usec;
    uint64_t p70_usec;
    uint64_t p90_usec;
    uint64_t p99_usec;
} IODiagnosticLatencyPercentiles;

typedef const IODiagnosticLatencyPercentiles * ConstIODiagnosticLatencyPercentilesPtr;

typedef struct BlockDeviceTiming
{
    uint64_t device_irq_usec;
    uint64_t manager_wake_usec;
    uint64_t irq_relay_usec;
    uint64_t fiber_wake_usec;
    uint64_t completion_usec;
} BlockDeviceTiming;

OptionalDef(BlockDeviceTiming);

typedef struct IODiagnostics
{
    uint64_t uart_bytes;
    uint64_t uart_irqs;
    uint64_t uart_reads;
    uint64_t uart_empty;
    uint64_t backend_reads;
    uint64_t backend_empty;
    uint64_t warden_relays;
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
    IODiagnosticLatency block_device;
    IODiagnosticLatency block_device_irq;
    IODiagnosticLatency block_manager_wake;
    IODiagnosticLatency block_irq_relay;
    IODiagnosticLatency block_fiber_wake;
    IODiagnosticLatency block_completion;
} IODiagnostics;

typedef IODiagnostics * IODiagnosticsPtr;
typedef const IODiagnostics * ConstIODiagnosticsPtr;
OptionalDef(IODiagnosticsPtr);

typedef struct IODiagnosticsSnapshot
{
    uint64_t uart_bytes;
    uint64_t uart_irqs;
    uint64_t uart_reads;
    uint64_t uart_empty;
    uint64_t backend_reads;
    uint64_t backend_empty;
    uint64_t warden_relays;
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
    IODiagnosticLatencyPercentiles block_device;
    IODiagnosticLatencyPercentiles block_device_irq;
    IODiagnosticLatencyPercentiles block_manager_wake;
    IODiagnosticLatencyPercentiles block_irq_relay;
    IODiagnosticLatencyPercentiles block_fiber_wake;
    IODiagnosticLatencyPercentiles block_completion;
} IODiagnosticsSnapshot;

OptionalDef(IODiagnosticsSnapshot);

// The process owns the counters. They are advisory, never scheduler inputs.
void IODiagnostics__attach(IODiagnosticsPtr self);
void IODiagnostics__record(IODiagnosticEvent event, uint64_t amount);
void IODiagnosticLatency__record(IODiagnosticLatencyPtr self, uint64_t value);
uint64_t IODiagnosticLatency__percentile(ConstIODiagnosticLatencyPtr self, uint64_t percentile);
Optional(BlockDeviceTiming) BlockDeviceTiming__new(uint64_t notified_at_usec, uint64_t kernel_irq_at_usec,
                                                   uint64_t manager_received_at_usec, uint64_t driver_received_at_usec,
                                                   uint64_t fiber_resumed_at_usec, uint64_t used_observed_at_usec);
void IODiagnostics__record_block_submission(uint64_t blocks, uint64_t device_usec, bool succeeded,
                                            OptionalPtr(BlockDeviceTiming) timing);
IODiagnosticsSnapshot IODiagnostics__snapshot(ConstIODiagnosticsPtr self);
#endif

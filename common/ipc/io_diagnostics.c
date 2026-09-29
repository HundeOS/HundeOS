#include "io_diagnostics.h"

static Optional(IODiagnosticsPtr) counters = None(IODiagnosticsPtr);

static uint64_t IODiagnosticLatency__bucket_index(uint64_t value)
{
    if (value == 0)
    {
        return 0;
    }

    uint64_t exponent = 0;
    for (uint64_t shifted = value; shifted > 1U; shifted >>= 1U)
    {
        exponent++;
    }
    uint64_t base = 1ULL << exponent;
    uint64_t sub_bucket = exponent < 2U ? (value - base) * IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT / base
                                        : (value - base) / (base / IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT);
    uint64_t index = 1U + exponent * IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT + sub_bucket;
    return index < IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT ? index : IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT - 1U;
}

static uint64_t IODiagnosticLatency__bucket_upper_bound(uint64_t index)
{
    if (index == 0)
    {
        return 0;
    }

    uint64_t positive_index = index - 1U;
    uint64_t exponent = positive_index / IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT;
    uint64_t sub_bucket = positive_index % IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT;
    uint64_t base = 1ULL << exponent;
    uint64_t numerator = (sub_bucket + 1U) * base;
    return base + (numerator + IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT - 1U) / IO_DIAGNOSTIC_LATENCY_SUB_BUCKET_COUNT -
           1U;
}

void IODiagnosticLatency__record(IODiagnosticLatencyPtr self, uint64_t value)
{
    uint64_t index = IODiagnosticLatency__bucket_index(value);
    __atomic_fetch_add(&self->buckets[index], 1, __ATOMIC_RELAXED);
}

uint64_t IODiagnosticLatency__percentile(ConstIODiagnosticLatencyPtr self, uint64_t percentile)
{
    if (percentile == 0 || percentile > 100)
    {
        return 0;
    }

    uint64_t samples = 0;
    for (uint64_t index = 0; index < IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT; index++)
    {
        samples += __atomic_load_n(&self->buckets[index], __ATOMIC_RELAXED);
    }
    if (samples == 0)
    {
        return 0;
    }

    uint64_t rank = samples / 100U * percentile + (samples % 100U * percentile + 99U) / 100U;
    uint64_t observed = 0;
    for (uint64_t index = 0; index < IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT; index++)
    {
        observed += __atomic_load_n(&self->buckets[index], __ATOMIC_RELAXED);
        if (observed >= rank)
        {
            return IODiagnosticLatency__bucket_upper_bound(index);
        }
    }
    return IODiagnosticLatency__bucket_upper_bound(IO_DIAGNOSTIC_LATENCY_BUCKET_COUNT - 1U);
}

Optional(BlockDeviceTiming) BlockDeviceTiming__new(uint64_t notified_at_usec, uint64_t kernel_irq_at_usec,
                                                   uint64_t manager_received_at_usec, uint64_t driver_received_at_usec,
                                                   uint64_t fiber_resumed_at_usec, uint64_t used_observed_at_usec)
{
    if (kernel_irq_at_usec < notified_at_usec || manager_received_at_usec < kernel_irq_at_usec ||
        driver_received_at_usec < manager_received_at_usec || fiber_resumed_at_usec < driver_received_at_usec ||
        used_observed_at_usec < fiber_resumed_at_usec)
    {
        return None(BlockDeviceTiming);
    }
    return Some(BlockDeviceTiming,
                ((BlockDeviceTiming){.device_irq_usec = kernel_irq_at_usec - notified_at_usec,
                                     .manager_wake_usec = manager_received_at_usec - kernel_irq_at_usec,
                                     .irq_relay_usec = driver_received_at_usec - manager_received_at_usec,
                                     .fiber_wake_usec = fiber_resumed_at_usec - driver_received_at_usec,
                                     .completion_usec = used_observed_at_usec - fiber_resumed_at_usec}));
}

void IODiagnostics__attach(IODiagnosticsPtr self)
{
    counters = Some(IODiagnosticsPtr, self);
}

void IODiagnostics__record(IODiagnosticEvent event, uint64_t amount)
{
    match_optional(
        counters,
        some(target)
        {
            switch (event)
            {
            case IO_DIAGNOSTIC_UART_BYTES:
                __atomic_fetch_add(&target->uart_bytes, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_IRQS:
                __atomic_fetch_add(&target->uart_irqs, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_READS:
                __atomic_fetch_add(&target->uart_reads, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UART_EMPTY:
                __atomic_fetch_add(&target->uart_empty, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BACKEND_READS:
                __atomic_fetch_add(&target->backend_reads, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BACKEND_EMPTY:
                __atomic_fetch_add(&target->backend_empty, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_WARDEN_RELAYS:
                __atomic_fetch_add(&target->warden_relays, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_WAIT_ENTRIES:
                __atomic_fetch_add(&target->wait_entries, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_UNCHANGED_WAKES:
                __atomic_fetch_add(&target->unchanged_wakes, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BLOCK_CACHE_HITS:
                __atomic_fetch_add(&target->block_cache_hits, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BLOCK_CACHE_MISSES:
                __atomic_fetch_add(&target->block_cache_misses, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_BLOCK_CACHE_EVICTIONS:
                __atomic_fetch_add(&target->block_cache_evictions, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_INODE_CACHE_HITS:
                __atomic_fetch_add(&target->inode_cache_hits, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_INODE_CACHE_MISSES:
                __atomic_fetch_add(&target->inode_cache_misses, amount, __ATOMIC_RELAXED);
                break;
            case IO_DIAGNOSTIC_INODE_CACHE_EVICTIONS:
                __atomic_fetch_add(&target->inode_cache_evictions, amount, __ATOMIC_RELAXED);
                break;
            }
        },
        none_do_nothing()
    );
}

void IODiagnostics__record_block_submission(uint64_t blocks, uint64_t device_usec, bool succeeded,
                                            OptionalPtr(BlockDeviceTiming) timing)
{
    match_optional(
        counters,
        some(target)
        {
            __atomic_fetch_add(&target->block_submissions, 1, __ATOMIC_RELAXED);
            __atomic_fetch_add(&target->block_blocks, blocks, __ATOMIC_RELAXED);
            if (!succeeded)
            {
                __atomic_fetch_add(&target->block_failures, 1, __ATOMIC_RELAXED);
            }
            IODiagnosticLatency__record(&target->block_device, device_usec);
            match_optional(
                *timing,
                some(segments)
                {
                    __atomic_fetch_add(&target->block_segmented_submissions, 1, __ATOMIC_RELAXED);
                    IODiagnosticLatency__record(&target->block_device_irq, segments.device_irq_usec);
                    IODiagnosticLatency__record(&target->block_manager_wake, segments.manager_wake_usec);
                    IODiagnosticLatency__record(&target->block_irq_relay, segments.irq_relay_usec);
                    IODiagnosticLatency__record(&target->block_fiber_wake, segments.fiber_wake_usec);
                    IODiagnosticLatency__record(&target->block_completion, segments.completion_usec);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
}

static IODiagnosticLatencyPercentiles IODiagnosticLatencyPercentiles__new(ConstIODiagnosticLatencyPtr latency)
{
    return (IODiagnosticLatencyPercentiles){.p50_usec = IODiagnosticLatency__percentile(latency, 50),
                                            .p70_usec = IODiagnosticLatency__percentile(latency, 70),
                                            .p90_usec = IODiagnosticLatency__percentile(latency, 90),
                                            .p99_usec = IODiagnosticLatency__percentile(latency, 99)};
}

static IODiagnosticsSnapshot IODiagnosticsSnapshot__new(ConstIODiagnosticsPtr self)
{
    return (IODiagnosticsSnapshot){
        .uart_bytes = __atomic_load_n(&self->uart_bytes, __ATOMIC_RELAXED),
        .uart_irqs = __atomic_load_n(&self->uart_irqs, __ATOMIC_RELAXED),
        .uart_reads = __atomic_load_n(&self->uart_reads, __ATOMIC_RELAXED),
        .uart_empty = __atomic_load_n(&self->uart_empty, __ATOMIC_RELAXED),
        .backend_reads = __atomic_load_n(&self->backend_reads, __ATOMIC_RELAXED),
        .backend_empty = __atomic_load_n(&self->backend_empty, __ATOMIC_RELAXED),
        .warden_relays = __atomic_load_n(&self->warden_relays, __ATOMIC_RELAXED),
        .wait_entries = __atomic_load_n(&self->wait_entries, __ATOMIC_RELAXED),
        .unchanged_wakes = __atomic_load_n(&self->unchanged_wakes, __ATOMIC_RELAXED),
        .block_cache_hits = __atomic_load_n(&self->block_cache_hits, __ATOMIC_RELAXED),
        .block_cache_misses = __atomic_load_n(&self->block_cache_misses, __ATOMIC_RELAXED),
        .block_cache_evictions = __atomic_load_n(&self->block_cache_evictions, __ATOMIC_RELAXED),
        .inode_cache_hits = __atomic_load_n(&self->inode_cache_hits, __ATOMIC_RELAXED),
        .inode_cache_misses = __atomic_load_n(&self->inode_cache_misses, __ATOMIC_RELAXED),
        .inode_cache_evictions = __atomic_load_n(&self->inode_cache_evictions, __ATOMIC_RELAXED),
        .block_submissions = __atomic_load_n(&self->block_submissions, __ATOMIC_RELAXED),
        .block_blocks = __atomic_load_n(&self->block_blocks, __ATOMIC_RELAXED),
        .block_failures = __atomic_load_n(&self->block_failures, __ATOMIC_RELAXED),
        .block_segmented_submissions = __atomic_load_n(&self->block_segmented_submissions, __ATOMIC_RELAXED),
        .block_device = IODiagnosticLatencyPercentiles__new(&self->block_device),
        .block_device_irq = IODiagnosticLatencyPercentiles__new(&self->block_device_irq),
        .block_manager_wake = IODiagnosticLatencyPercentiles__new(&self->block_manager_wake),
        .block_irq_relay = IODiagnosticLatencyPercentiles__new(&self->block_irq_relay),
        .block_fiber_wake = IODiagnosticLatencyPercentiles__new(&self->block_fiber_wake),
        .block_completion = IODiagnosticLatencyPercentiles__new(&self->block_completion),
    };
}

IODiagnosticsSnapshot IODiagnostics__snapshot(ConstIODiagnosticsPtr self)
{
    return IODiagnosticsSnapshot__new(self);
}

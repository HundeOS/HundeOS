#include "native.h"
#include "sys/process.h"
#include "sys/mem.h"
#include "sys/system_info.h"
#include "memory.h"

_Static_assert(BSTRING_MAX_SIZE <= 1024, "Process name exceeds ABI text capacity");

static HundeProcessText HundeProcessText__new(ConstBStringPtr text)
{
    HundeProcessText value = {.length = BString__length(text)};
    Mem__memcpy(value.bytes, BString__cstr(text), value.length);
    return value;
}

static HundeLatency HundeLatency__new(ConstIODiagnosticLatencyPercentilesPtr self)
{
    return (HundeLatency){
        .p50_usec = self->p50_usec, .p70_usec = self->p70_usec, .p90_usec = self->p90_usec, .p99_usec = self->p99_usec};
}

static HundeProcessSnapshot HundeProcessSnapshot__new(ConstProcessRecordPtr record)
{
    ProcessInfo info = ProcessRecord__info(record);
    BString state = bstring(info.state);
    BString wait = bstring(info.wait_reason);
    BString class_name = bstring(info.class_name);
    HundeProcessSnapshot value = {
        .pid = record->pid,
        .cpu = record->cpu,
        .sampled_at_usec = record->sampled_at_usec,
        .user_id = record->credentials.user_id,
        .group_id = record->credentials.group_id,
        .user_wait_usec = record->user_wait_usec,
        .name = HundeProcessText__new(&record->name),
        .state = HundeProcessText__new(&state),
        .wait = HundeProcessText__new(&wait),
        .class_name = HundeProcessText__new(&class_name),
        .cpu_usage = {.total_usec = record->cpu_usage.total_usec,
                      .user_usec = record->cpu_usage.user_usec,
                      .system_usec = record->cpu_usage.system_usec,
                      .short_basis_points = record->cpu_usage.short_basis_points,
                      .long_basis_points = record->cpu_usage.long_basis_points,
                      .scheduled_usec = record->cpu_usage.scheduled_usec,
                      .scheduled_short_basis_points = record->cpu_usage.scheduled_short_basis_points,
                      .scheduled_long_basis_points = record->cpu_usage.scheduled_long_basis_points},
        .queue_wait = {.parks = record->queue_wait_usage.parks,
                       .activity_wakes = record->queue_wait_usage.activity_wakes,
                       .timeout_wakes = record->queue_wait_usage.timeout_wakes,
                       .selections = record->queue_wait_usage.selections,
                       .speculative_selections = record->queue_wait_usage.speculative_selections,
                       .parked_usec = record->queue_wait_usage.parked_usec,
                       .max_parked_usec = record->queue_wait_usage.max_parked_usec,
                       .runnable_usec = record->queue_wait_usage.runnable_usec,
                       .max_runnable_usec = record->queue_wait_usage.max_runnable_usec},
    };
    match_optional(
        record->memory,
        some(memory)
        {
            value.has_memory = 1;
            value.memory.private_bytes = memory.private_bytes;
            value.memory.shared_bytes = memory.shared_bytes;
            value.memory.device_bytes = memory.device_bytes;
        },
        none_do_nothing()
    );
    match_optional(
        record->io_diagnostics,
        some(diagnostics)
        {
            value.has_diagnostics = 1;
            value.diagnostics.uart_bytes = diagnostics.uart_bytes;
            value.diagnostics.uart_irqs = diagnostics.uart_irqs;
            value.diagnostics.uart_reads = diagnostics.uart_reads;
            value.diagnostics.uart_empty = diagnostics.uart_empty;
            value.diagnostics.backend_reads = diagnostics.backend_reads;
            value.diagnostics.backend_empty = diagnostics.backend_empty;
            value.diagnostics.wait_entries = diagnostics.wait_entries;
            value.diagnostics.unchanged_wakes = diagnostics.unchanged_wakes;
            value.diagnostics.block_cache_hits = diagnostics.block_cache_hits;
            value.diagnostics.block_cache_misses = diagnostics.block_cache_misses;
            value.diagnostics.block_cache_evictions = diagnostics.block_cache_evictions;
            value.diagnostics.inode_cache_hits = diagnostics.inode_cache_hits;
            value.diagnostics.inode_cache_misses = diagnostics.inode_cache_misses;
            value.diagnostics.inode_cache_evictions = diagnostics.inode_cache_evictions;
            value.diagnostics.block_submissions = diagnostics.block_submissions;
            value.diagnostics.block_blocks = diagnostics.block_blocks;
            value.diagnostics.block_failures = diagnostics.block_failures;
            value.diagnostics.block_segmented_submissions = diagnostics.block_segmented_submissions;
            value.diagnostics.block_device = HundeLatency__new(&diagnostics.block_device);
            value.diagnostics.block_device_irq = HundeLatency__new(&diagnostics.block_device_irq);
            value.diagnostics.block_manager_wake = HundeLatency__new(&diagnostics.block_manager_wake);
            value.diagnostics.block_irq_relay = HundeLatency__new(&diagnostics.block_irq_relay);
            value.diagnostics.block_fiber_wake = HundeLatency__new(&diagnostics.block_fiber_wake);
            value.diagnostics.block_completion = HundeLatency__new(&diagnostics.block_completion);
        },
        none_do_nothing()
    );
    return value;
}

static HundeProcessResult HundeProcessResult__new_error(int32_t code)
{
    return (HundeProcessResult){.kind = HUNDE_ABI_ERROR,
                                .error = {.code = code, .message = {(const uint8_t *)"Process snapshot failed", 23}}};
}

static HundeProcessResult HundeProcessResult__new(OptionalProcessRecord found)
{
    match_optional(
        found,
        some(record)
        {
            return ((HundeProcessResult){
                .kind = HUNDE_ABI_SUCCESS, .found = 1, .snapshot = HundeProcessSnapshot__new(&record)});
        },
        none()
        {
            return ((HundeProcessResult){.kind = HUNDE_ABI_SUCCESS});
        }
    );
    return HundeProcessResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

HundeProcessResult hunde_rust_process(uint64_t selector, uint32_t by_index)
{
    if (by_index > 1)
    {
        return HundeProcessResult__new_error(IO_OPERATION_NOT_SUPPORTED);
    }
    match_result(
        process_registry_map(),
        success(registry)
        {
            match_result(
                by_index ? ProcessRegistry__at(registry, selector) : ProcessRegistry__find(registry, selector),
                success(found)
                {
                    return HundeProcessResult__new(found);
                },
                error(code, message)
                {
                    return HundeProcessResult__new_error(code == ATOMIC_RECORD_BUSY ? IO_WAIT_FOR_DATA
                                                                                    : IO_NO_DEVICE_FOUND);
                }
            );
            return HundeProcessResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return HundeProcessResult__new_error(IO_NO_DEVICE_FOUND);
        }
    );
    return HundeProcessResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

static HundeTraceResult HundeTraceResult__new(uint64_t size)
{
    return (HundeTraceResult){.kind = HUNDE_ABI_SUCCESS, .size = size};
}

static HundeTraceResult HundeTraceResult__new_error(int32_t code)
{
    return (HundeTraceResult){.kind = HUNDE_ABI_ERROR,
                              .error = {.code = code, .message = {(const uint8_t *)"Trace snapshot failed", 21}}};
}

static HundeTraceResult HundeTrace__copy(uint8_t * destination, uint8_t * buffer, uint64_t capacity)
{
    match_result(
        KernelTrace__load(buffer, capacity),
        success(size)
        {
            if (size > capacity)
            {
                return HundeTraceResult__new_error(IO_UNKNOWN_BUFFER);
            }
            Mem__memcpy(destination, buffer, size);
            return HundeTraceResult__new(size);
        },
        error(code, message)
        {
            return HundeTraceResult__new_error(IO_NO_DEVICE_FOUND);
        }
    );
    return HundeTraceResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

HundeTraceResult hunde_rust_kernel_trace(uint8_t * destination, uint64_t capacity)
{
    if (capacity == 0 || capacity > KERNEL_TRACE_CAPACITY)
    {
        return HundeTraceResult__new_error(IO_UNKNOWN_BUFFER);
    }
    match_result(
        mem_alloc_pages_result(capacity),
        success(memory)
        {
            uint8_t * buffer = (uint8_t *)memory;
            HundeTraceResult result = HundeTrace__copy(destination, buffer, capacity);
            if (mem_free_pages(buffer) != MEM_OK)
            {
                return HundeTraceResult__new_error(IO_REGISTER_FAILED);
            }
            return result;
        },
        error(code, message)
        {
            return HundeTraceResult__new_error(IO_REGISTER_FAILED);
        }
    );
    return HundeTraceResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

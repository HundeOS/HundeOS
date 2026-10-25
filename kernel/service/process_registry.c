#include "process_registry.h"
#include "../kernel_core.h"
#include "../arch/cpu.h"
#include "../scheduler/shared_wait.h"
#include "ipc/queue_wait_control.h"

_Static_assert(MAX_CPUS == PROCESS_REGISTRY_CPUS, "Process registry CPU capacity mismatch");
_Static_assert(MAX_PROCESSES + 1 == PROCESS_REGISTRY_CPU_CAPACITY, "Process registry capacity mismatch");

Result(ServiceCode) KernelProcessRegistry__map_process(KernelProcessRegistryPtr self, ProcessPtr process)
{
    critical_section(
        self->lock,
        {
            if (process->process_registry_mapped)
            {
                cs_return(Success(ServiceCode, SERVICE_OK));
            }
            if (!self->mapping.valid)
            {
                match_result(
                    Kernel__try_alloc_user_aligned(PROCESS_REGISTRY_VADDR, PROCESS_REGISTRY_SIZE, _2MB,
                                                   true, false, false),
                    success(mapping)
                    {
                        Kernel__memset(mapping.kernel_addr.virtual_addr, 0, PROCESS_REGISTRY_SIZE);
                        k_v_ptr(mapping, ProcessRegistry)->version = PROCESS_REGISTRY_VERSION;
                        self->mapping = mapping;
                    },
                    error(code, message)
                    {
                        cs_return(Error(ServiceCode, message, SERVICE_MAP_FAILED));
                    }
                );
            }
            match_result(
                PageTable__try_map_shared_blocks(&process->page_table, process->kernel, self->mapping,
                                                 PROCESS_REGISTRY_SIZE),
                success_do_nothing(),
                error(code, message)
                {
                    cs_return(Error(ServiceCode, message, SERVICE_MAP_FAILED));
                }
            );
            process->process_registry_mapped = true;
            cs_return(Success(ServiceCode, SERVICE_OK));
        }
    );
}

void KernelProcessRegistry__set(KernelProcessRegistryPtr self, uint64_t cpu, uint64_t index,
                                ConstProcessRecordPtr record)
{
    if (self->mapping.valid)
    {
        ProcessRegistry__set(k_v_ptr(self->mapping, ProcessRegistry), cpu, index, record);
    }
}

void KernelProcessRegistry__set_count(KernelProcessRegistryPtr self, uint64_t cpu, uint64_t count)
{
    if (self->mapping.valid)
    {
        ProcessRegistry__set_count(k_v_ptr(self->mapping, ProcessRegistry), cpu, count);
    }
}

ProcessRecord ProcessRecord__new(ProcessPtr process, uint64_t cpu)
{
    uint64_t sampled_at_usec = CPU__time_usec();
    Optional(IODiagnosticsSnapshot) diagnostics = None(IODiagnosticsSnapshot);
    Optional(ProcessUserWait) user_wait = None(ProcessUserWait);
    uint64_t user_wait_usec = Process__user_wait_usec(process, sampled_at_usec);
    match_optional(
        process->queue_wait_control,
        some(mapping)
        {
            ConstQueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
            diagnostics = Some(IODiagnosticsSnapshot, IODiagnostics__snapshot(&control->diagnostics));
        },
        none_do_nothing()
    );
    match_optional(
        Process__shared_wait(process),
        some(publication)
        {
            uint64_t started_at_usec = publication.started_at_usec;
            if (started_at_usec > sampled_at_usec)
            {
                started_at_usec = sampled_at_usec;
            }
            user_wait = Some(ProcessUserWait, ((ProcessUserWait){.started_at_usec = started_at_usec,
                                                                 .elapsed_usec = sampled_at_usec - started_at_usec}));
        },
        none_do_nothing()
    );
    return (ProcessRecord){.pid = process->pid,
                           .cpu = cpu,
                           .sampled_at_usec = sampled_at_usec,
                           .name = process->name,
                           .state = process->state,
                           .wait_reason = Process__top_park_reason(process),
                           .user_wait = user_wait,
                           .user_wait_usec = user_wait_usec,
                           .process_class = process->process_class,
                           .credentials = ProcessCredentials__new(process->user_id, process->group_id),
                           .cpu_usage = CPUUsage__snapshot(&process->cpu_usage, sampled_at_usec, user_wait_usec),
                           .queue_wait_usage = QueueWaitUsage__snapshot(&process->queue_wait_usage),
                           .io_diagnostics = diagnostics,
                           .memory =
                               MemoryAccounting__usage(&process->kernel->memory_accounting,
                                                       ProcessPageTable__root(&process->page_table).virtual_addr)};
}

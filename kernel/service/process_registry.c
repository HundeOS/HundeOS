#include "process_registry.h"
#include "../kernel_core.h"
#include "../arch/cpu.h"

_Static_assert(MAX_CPUS == PROCESS_REGISTRY_CPUS, "Process registry CPU capacity mismatch");
_Static_assert(MAX_PROCESSES + 1 == PROCESS_REGISTRY_CPU_CAPACITY, "Process registry capacity mismatch");

Result(ServiceCode) KernelProcessRegistry__map_process(KernelProcessRegistryPtr self, ProcessPtr process)
{
    critical_section(self->lock, {
        if (!self->mapping.valid)
        {
            match_result(
                Kernel__try_alloc_user(PROCESS_REGISTRY_VADDR, PROCESS_REGISTRY_SIZE, true, false, false),
                success(mapping)
                {
                    Kernel__memset(mapping.kernel_addr.virtual_addr, 0, PROCESS_REGISTRY_SIZE);
                    k_v_ptr(mapping, ProcessRegistry)->version = PROCESS_REGISTRY_VERSION;
                    self->mapping = mapping;
                },
                error(code, message) { cs_return Error(ServiceCode, message, SERVICE_MAP_FAILED); }
            );
        }
        PageTable__map_shared(&process->page_table, process->kernel, self->mapping, PROCESS_REGISTRY_SIZE);
        cs_return Success(ServiceCode, SERVICE_OK);
    });
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
    return (ProcessRecord){.pid = process->pid,
                           .cpu = cpu,
                           .sampled_at_usec = CPU__time_usec(),
                           .name = process->name,
                           .state = process->state,
                           .wait_reason = Process__top_park_reason(process),
                           .process_class = process->process_class,
                           .credentials = ProcessCredentials__new(process->user_id, process->group_id),
                           .cpu_usage = CPUUsage__snapshot(&process->cpu_usage),
                           .memory =
                               MemoryAccounting__usage(&process->kernel->memory_accounting,
                                                       ProcessPageTable__root(&process->page_table).virtual_addr)};
}

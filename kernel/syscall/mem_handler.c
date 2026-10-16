#include "mem_handler.h"

#include "syscalls/mem.h"
#include "../kernel_core.h"

static BString no_process = bstr("No process for memory syscall");
static BString invalid_size = bstr("Invalid memory allocation size");
static BString unsupported_operation = bstr("Unsupported memory operation");

static MemStatus Mem__status_from_process_code(ProcessMemoryCode code)
{
    switch (code)
    {
    case PROCESS_MEMORY_OK:
        return MEM_OK;
    case PROCESS_MEMORY_NOT_FOUND:
    case PROCESS_MEMORY_STATIC_ALLOCATION:
        return MEM_INVALID_ADDRESS;
    case PROCESS_MEMORY_FULL:
        return MEM_ALLOCATION_FAILED;
    case PROCESS_MEMORY_FREE_FAILED:
        return MEM_FREE_FAILED;
    case PROCESS_MEMORY_INVALID_SIZE:
        return MEM_INVALID_SIZE;
    }

    return MEM_OPERATION_NOT_SUPPORTED;
}

static void Mem__terminate_on_allocation_failure(ProcessPtr process, ProcessMemoryCode code)
{
    if (code == PROCESS_MEMORY_FULL)
    {
        Process__queue_signal(process, PROCESS_SIGNAL_TERMINATE);
    }
}

static MemPressureLevel Mem__pressure_level(HeapPressureLevel pressure)
{
    switch (pressure)
    {
    case HEAP_PRESSURE_NORMAL:
        return MEM_PRESSURE_NORMAL;
    case HEAP_PRESSURE_LOW:
        return MEM_PRESSURE_LOW;
    case HEAP_PRESSURE_CRITICAL:
        return MEM_PRESSURE_CRITICAL;
    }
    return MEM_PRESSURE_CRITICAL;
}

static void Mem__notify_pressure(KernelPtr kernel)
{
    HeapMemoryState state = Heap__memory_state(&kernel->heap);
    match_result(
        Scheduler__notify_memory_pressure(kernel->scheduler, state.pressure_generation,
                                          Mem__pressure_level(state.pressure_level), state.free_pages),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(MEMORY, "memory pressure notification failed code=%i message=%s\n", code,
                       BString__cstr(&message));
        }
    );
}

Result(SystemCallResult) Mem__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything mem_operation,
                                      Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    match_optional(
        *proc,
        some(process)
        {
            if (mem_operation >= MEM_BUFFER_REGISTER && mem_operation <= MEM_BUFFER_DEVICE_ADDRESS)
            {
                SchedulerPtr scheduler = kernel->scheduler;
                critical_section(
                    scheduler->scheduler_lock,
                    {
                        match_result(
                            Buffers__operation(&scheduler->buffers, scheduler, process, mem_operation,
                                               arg2, arg3, arg4, arg5),
                            success(value)
                            {
                                cs_return Success(SystemCallResult, value);
                            },
                            error(code, message)
                            {
                                cs_return Error(SystemCallResult, message, code);
                            }
                        );
                    }
                );
            }
            if (mem_operation == MEM_ALLOC_SHARED_PAGE)
            {
                match_result(
                    Scheduler__alloc_shared_page(kernel->scheduler, process, arg2, arg3),
                    success(address)
                    {
                        return Success(SystemCallResult, address);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            }
            if (mem_operation == MEM_RELEASE_SHARED_PAGE)
            {
                match_result(
                    Scheduler__release_shared_page(kernel->scheduler, process, arg2),
                    success(status)
                    {
                        return Success(SystemCallResult, status);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
            }
            if (mem_operation == MEM_ALLOC_PAGES)
            {
                size_t size = (size_t)arg2;
                if (size == 0)
                {
                    return Error(SystemCallResult, invalid_size, MEM_INVALID_SIZE);
                }

                match_result(
                    Process__alloc_user_pages(process, size),
                    success(address)
                    {
                        Mem__notify_pressure(kernel);
                        return Success(SystemCallResult, address.virtual_addr);
                    },
                    error(code, message)
                    {
                        Mem__terminate_on_allocation_failure(process, code);
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            if (mem_operation == MEM_ALLOC_RECLAIMABLE_PAGES)
            {
                size_t size = (size_t)arg2;
                if (size == 0)
                {
                    return Error(SystemCallResult, invalid_size, MEM_INVALID_SIZE);
                }
                if (!ProcessClass__is_service(process->process_class))
                {
                    return Error(SystemCallResult, bstr("Reclaimable allocations require a service process"),
                                 MEM_PERMISSION_DENIED);
                }
                HeapMemoryState state = Heap__memory_state(&kernel->heap);
                match_result(
                    Scheduler__register_memory_reclaimer(kernel->scheduler, process,
                                                         Mem__pressure_level(state.pressure_level), state.free_pages),
                    success_do_nothing(),
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, code);
                    }
                );
                match_result(
                    Process__alloc_reclaimable_user_pages(process, size),
                    success(address)
                    {
                        Mem__notify_pressure(kernel);
                        return Success(SystemCallResult, address.virtual_addr);
                    },
                    error(code, message)
                    {
                        Mem__notify_pressure(kernel);
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            if (mem_operation == MEM_FREE_PAGES)
            {
                match_result(
                    Process__free_user_pages(process, (Address)arg2),
                    success(_)
                    {
                        Mem__notify_pressure(kernel);
                        return Success(SystemCallResult, MEM_OK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            if (mem_operation == MEM_SBRK)
            {
                match_result(
                    Process__sbrk(process, (int64_t)arg2),
                    success(old_break)
                    {
                        Mem__notify_pressure(kernel);
                        return Success(SystemCallResult, old_break);
                    },
                    error(code, message)
                    {
                        Mem__terminate_on_allocation_failure(process, code);
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            return Error(SystemCallResult, unsupported_operation, MEM_OPERATION_NOT_SUPPORTED);
        },
        none()
        {
            return Error(SystemCallResult, no_process, MEM_NO_PROCESS);
        }
    );

    return Error(SystemCallResult, unsupported_operation, MEM_OPERATION_NOT_SUPPORTED);
}

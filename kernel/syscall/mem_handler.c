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

Result(SystemCallResult) Mem__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) proc, Anything mem_operation,
                                      Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    match_optional(
        *proc,
        some(process)
        {
            if (mem_operation == MEM_ALLOC_SHARED_PAGE)
            {
                match_result(
                    Scheduler__alloc_shared_page(kernel->scheduler, process, arg2, arg3),
                    success(address) { return Success(SystemCallResult, address); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }
            if (mem_operation == MEM_RELEASE_SHARED_PAGE)
            {
                match_result(
                    Scheduler__release_shared_page(kernel->scheduler, process, arg2),
                    success(status) { return Success(SystemCallResult, status); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
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
                    success(address) { return Success(SystemCallResult, address.virtual_addr); },
                    error(code, message)
                    {
                        Mem__terminate_on_allocation_failure(process, code);
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            if (mem_operation == MEM_FREE_PAGES)
            {
                match_result(
                    Process__free_user_pages(process, (Address)arg2),
                    success(_) { return Success(SystemCallResult, MEM_OK); },
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
                    success(old_break) { return Success(SystemCallResult, old_break); },
                    error(code, message)
                    {
                        Mem__terminate_on_allocation_failure(process, code);
                        return Error(SystemCallResult, message, Mem__status_from_process_code(code));
                    }
                );
            }

            return Error(SystemCallResult, unsupported_operation, MEM_OPERATION_NOT_SUPPORTED);
        },
        none() { return Error(SystemCallResult, no_process, MEM_NO_PROCESS); }
    );

    return Error(SystemCallResult, unsupported_operation, MEM_OPERATION_NOT_SUPPORTED);
}

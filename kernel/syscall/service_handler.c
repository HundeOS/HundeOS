#include "service_handler.h"

#include "../kernel_core.h"
#include "../service/service.h"

static KString no_process = KSTRING_NEW("No process for service syscall");
static KString unsupported_operation = KSTRING_NEW("Unsupported service operation");

Result(SystemCallResult) Service__handler(
    KernelPtr kernel, Optional(ProcessPtr) process_opt, Anything service_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{
    (void) arg3;
    (void) arg4;
    (void) arg5;

    Match(ProcessPtr, process_opt)
        IsSome(process,
        {
            if (service_operation == SERVICE_WAIT)
            {
                if (ServiceEvents__len(&process->service_events) == 0)
                {
                    Scheduler__complete_pending_irqs_for_process(kernel->scheduler, process);
                }
                KernelContext kernel_context = KernelContext__new(
                    Some(ProcessID, process->pid), Some(SchedulerPtr, kernel->scheduler)
                );
                ServiceEventPtr target = (ServiceEventPtr) arg2;

                ResultMatch(ServiceCode, Service__wait_to_buffer(process, &kernel_context, target))
                    IsSuccess(code, return Success(SystemCallResult, code))
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            }

            if (service_operation == SERVICE_MAP_MMIO)
            {
                ServiceMappedRangePtr range = (ServiceMappedRangePtr) arg2;
                if (range == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                ResultMatch(UserAddress, Process__map_mmio(process, range->physical, range->size))
                    IsSuccess(address,
                    {
                        range->address = (void *) address.virtual_addr;
                        return Success(SystemCallResult, SERVICE_OK);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, SERVICE_MAP_FAILED))
                End
            }

            if (service_operation == SERVICE_SUBSCRIBE_IRQ)
            {
                uint64_t irq = (uint64_t) arg2;
                ResultMatch(ServiceCode, Scheduler__subscribe_irq(kernel->scheduler, process, irq))
                    IsSuccess(code, return Success(SystemCallResult, code))
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            }
        })
        IsNone(return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS))
    End

    return Error(SystemCallResult, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

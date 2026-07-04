#include "service_handler.h"

#include "../kernel_core.h"
#include "../debug.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/parked.h"
#include "../service/service.h"
#include "../subsystems/io/block/block_device.h"
#include "../subsystems/io/file/register.h"

static KString no_process = KSTRING_NEW("No process for service syscall");
static KString unsupported_operation = KSTRING_NEW("Unsupported service operation");

typedef struct ServiceMappedMemoryRequest
{
    uint64_t physical_address;
    uint64_t size;
    uint64_t driver_address;
    uint64_t device_address;
}
ServiceMappedMemoryRequest;

typedef ServiceMappedMemoryRequest * ServiceMappedMemoryRequestPtr;

static ServiceCode Service__map_irq_registration_status(Code code)
{
    if (code == IRQ_ALREADY_REGISTERED)
    {
        return SERVICE_IRQ_ALREADY_REGISTERED;
    }
    if (code == IRQ_INVALID_CONSUMER)
    {
        return SERVICE_IRQ_INVALID_CONSUMER;
    }
    return SERVICE_IRQ_REGISTRATION_FAILED;
}

static ServiceCode Service__map_block_registration_status(Code code)
{
    if (code == BLOCK_DEVICE_ALREADY_REGISTERED)
    {
        return SERVICE_DEVICE_ALREADY_REGISTERED;
    }
    if (code == BLOCK_DEVICE_INVALID_ARGUMENT)
    {
        return SERVICE_INVALID_BUFFER;
    }
    return SERVICE_DEVICE_REGISTRATION_FAILED;
}

static Result(Anything) ServiceEvent__copy_to_target(Result(Anything) result, Anything context)
{
    ServiceEventPtr target = ptr(context, ServiceEvent);

    ResultMatch(Anything, result)
        IsSuccess(event_ptr,
        {
            ServiceEventPtr event = ptr(event_ptr, ServiceEvent);
            *target = *event;
            return Success(Anything, SERVICE_OK);
        })
        IsError(code, message, return Error(Anything, message, code))
    End

    return Error(Anything, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

static Result(Anything) Service__complete_wait_syscall(ParkedKernelContextPtr parked_context)
{
    ResultMatch(ServiceEvent, Service__complete_wait(parked_context))
        IsSuccess(event,
        {
            Result(Anything) mapped = KernelContext__map_result(
                parked_context->kernel_context,
                Success(Anything, (Anything) &event)
            );
            ParkedKernelContext__dispose(parked_context);
            return mapped;
        })
        IsError(code, message,
        {
            Result(Anything) mapped = KernelContext__map_result(
                parked_context->kernel_context,
                Error(Anything, message, code)
            );
            ParkedKernelContext__dispose(parked_context);
            return mapped;
        })
    End

    Result(Anything) mapped = KernelContext__map_result(
        parked_context->kernel_context,
        Error(Anything, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED)
    );
    ParkedKernelContext__dispose(parked_context);
    return mapped;
}

Result(SystemCallResult) Service__handler(
    KernelPtr kernel, Optional(ProcessPtr) process_opt, Anything service_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{

    Match(ProcessPtr, process_opt)
        IsSome(process,
        {
            if (service_operation == SERVICE_BECOME_SERVICE)
            {
                process->process_class = PROCESS_CLASS_SERVICE;
                return Success(SystemCallResult, SERVICE_OK);
            }

            if (service_operation == SERVICE_WAIT)
            {
                ServiceEventPtr target = (ServiceEventPtr) arg2;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                __d_printf(
                    IODEV,
                    "service wait pid=%i name=%s queued=%i target=%p\n",
                    process->pid,
                    cstr(process->name),
                    (int) ServiceEvents__len(&process->service.events),
                    arg2
                );

                KernelContextPtr kernel_context = KernelContext__heap_new_with_mapper(
                    Some(ProcessID, process->pid),
                    Some(SchedulerPtr, kernel->scheduler),
                    ServiceEvent__copy_to_target,
                    (Anything) target
                );

                ResultMatch(ServiceEvent, Scheduler__service_wait(
                    kernel->scheduler,
                    process,
                    kernel_context,
                    Some(Continuation, Service__complete_wait_syscall)
                ))
                    IsSuccess(event,
                    {
                        Result(Anything) mapped = KernelContext__map_result(
                            kernel_context,
                            Success(Anything, (Anything) &event)
                        );
                        KernelContext__dispose_heap(kernel_context);
                        __d_printf(IODEV, "service wait return pid=%i code=%i\n", process->pid, SERVICE_OK);
                        ResultMatch(Anything, mapped)
                            IsSuccess(mapped_code, return Success(SystemCallResult, mapped_code))
                            IsError(mapped_code, mapped_message, return Error(SystemCallResult, mapped_message, mapped_code))
                        End
                    })
                    IsError(code, message,
                    {
                        if (code != SERVICE_EVENT_QUEUE_EMPTY)
                        {
                            KernelContext__dispose_heap(kernel_context);
                            __d_printf(IODEV, "service wait error pid=%i code=%i\n", process->pid, code);
                        }
                        return Error(SystemCallResult, message, code);
                    })
                End
            }

            if (service_operation == SERVICE_MAP_MMIO)
            {
                ServiceMappedMemoryRequestPtr request = (ServiceMappedMemoryRequestPtr) arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                ResultMatch(UserAddress, Process__map_mmio(process, request->physical_address, request->size))
                    IsSuccess(address,
                    {
                        request->driver_address = address.virtual_addr;
                        request->device_address = request->physical_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, SERVICE_MAP_FAILED))
                End
            }

            if (service_operation == SERVICE_DEVICE_ADDRESS)
            {
                ServiceDeviceAddressRequestPtr request = (ServiceDeviceAddressRequestPtr) arg2;
                if (request == NULL || request->driver_address == 0)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                ResultMatch(Address, Process__device_address(process, request->driver_address))
                    IsSuccess(device_address,
                    {
                        request->device_address = device_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    })
                    IsError(code, message, return Error(SystemCallResult, message, SERVICE_INVALID_BUFFER))
                End
            }

            if (service_operation == SERVICE_SUBSCRIBE_IRQ)
            {
                uint64_t irq = (uint64_t) arg2;
                __d_printf(IODEV, "service subscribe irq pid=%i irq=%i\n", process->pid, (int) irq);
                ResultMatch(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, irq, process))
                    IsSuccess(_, return Success(SystemCallResult, SERVICE_OK))
                    IsError(
                        code,
                        message,
                        return Error(SystemCallResult, message, Service__map_irq_registration_status(code))
                    )
                End
            }

            if (service_operation == SERVICE_REGISTER_BLOCK_DEVICE)
            {
                ServiceBlockDeviceRegistrationPtr request = (ServiceBlockDeviceRegistrationPtr) arg2;
                if (request == NULL || request->block_size != BLOCK_DEVICE_BLOCK_SIZE)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                ResultMatch(BlockDeviceCode, BlockDeviceRegistry__register(
                    kstring(request->path), process, request->block_count, kernel->scheduler
                ))
                    IsSuccess(_,
                    {
                        Match(FilesystemPtr, kernel->io_subsystem->root_filesystem)
                            IsSome(root_fs, FilesystemTable__mount_available_entries(root_fs))
                        End
                        return Success(SystemCallResult, SERVICE_OK);
                    })
                    IsError(
                        code,
                        message,
                        return Error(SystemCallResult, message, Service__map_block_registration_status(code))
                    )
                End
            }

            if (service_operation == SERVICE_BLOCK_GET_REQUEST)
            {
                uint64_t request_id = (uint64_t) arg2;
                ServiceBlockRequestPtr target = (ServiceBlockRequestPtr) arg3;
                ResultMatch(ServiceCode, BlockDeviceService__get_request(process, request_id, target))
                    IsSuccess(code, return Success(SystemCallResult, code))
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            }

            if (service_operation == SERVICE_BLOCK_COMPLETE)
            {
                ServiceBlockCompletionPtr completion = (ServiceBlockCompletionPtr) arg2;
                ResultMatch(ServiceCode, BlockDeviceService__complete(process, completion))
                    IsSuccess(code, return Success(SystemCallResult, code))
                    IsError(code, message, return Error(SystemCallResult, message, code))
                End
            }
        })
        IsNone(return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS))
    End

    return Error(SystemCallResult, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

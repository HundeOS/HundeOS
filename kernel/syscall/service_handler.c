#include "service_handler.h"

#include "../kernel_core.h"
#include "../debug.h"
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

Result(BlockDeviceCode) BlockDeviceRegistry__register(
    KString path, ProcessPtr service_process, BlockCount block_count, SchedulerPtr scheduler
) __attribute__((weak));
Result(BlockDeviceCode) BlockDeviceRegistry__register(
    KString path, ProcessPtr service_process, BlockCount block_count, SchedulerPtr scheduler
)
{
    return Error(BlockDeviceCode, unsupported_operation, BLOCK_DEVICE_IO_ERROR);
}

Result(ServiceCode) BlockDeviceService__get_request(
    ProcessPtr service_process, uint64_t request_id, ServiceBlockRequestPtr target
) __attribute__((weak));
Result(ServiceCode) BlockDeviceService__get_request(
    ProcessPtr service_process, uint64_t request_id, ServiceBlockRequestPtr target
)
{
    return Error(ServiceCode, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
) __attribute__((weak));
Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
)
{
    return Error(ServiceCode, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

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
                __d_printf(
                    IODEV,
                    "service wait pid=%i name=%s queued=%i target=%p\n",
                    process->pid,
                    cstr(process->name),
                    (int) ServiceEvents__len(&process->service_events),
                    arg2
                );
                if (ServiceEvents__len(&process->service_events) == 0)
                {
                    Scheduler__complete_pending_irqs_for_process(kernel->scheduler, process);
                }
                KernelContext kernel_context = KernelContext__new(
                    Some(ProcessID, process->pid), Some(SchedulerPtr, kernel->scheduler)
                );
                ServiceEventPtr target = (ServiceEventPtr) arg2;

                ResultMatch(ServiceCode, Service__wait_to_buffer(process, &kernel_context, target))
                    IsSuccess(code,
                    {
                        __d_printf(IODEV, "service wait return pid=%i code=%i\n", process->pid, code);
                        return Success(SystemCallResult, code);
                    })
                    IsError(code, message,
                    {
                        __d_printf(IODEV, "service wait error pid=%i code=%i\n", process->pid, code);
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

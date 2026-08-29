#include "service_handler.h"

#include "../kernel_core.h"
#include "../debug.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/parked.h"
#include "../syscall/deferred.h"
#include "../service/service.h"
#include "../subsystems/io/block/block_device.h"
#include "../subsystems/io/file/register.h"
#include "../subsystems/io/file/userspace_filesystem.h"

static BString no_process = bstr("No process for service syscall");
static BString unsupported_operation = bstr("Unsupported service operation");
static BString permission_denied = bstr("Permission denied");

typedef struct ServiceMappedMemoryRequest
{
    Address physical_address;
    size_t size;
    Address driver_address;
    Address device_address;
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

static Result(SystemCallResult) Service__require_capability(ProcessPtr process, ProcessCapabilities capability)
{
    if (Process__has_capability(process, capability))
    {
        return Success(SystemCallResult, SERVICE_OK);
    }

    return Error(SystemCallResult, permission_denied, SERVICE_PERMISSION_DENIED);
}

static Result(Anything) ServiceEvent__copy_to_target(Result(Anything) result, Anything context)
{
    ServiceEventPtr target = ptr(context, ServiceEvent);

    match_result(Anything, result,
        success(event_ptr)
        {
            ServiceEventPtr event = ptr(event_ptr, ServiceEvent);
            *target = *event;
            return Success(Anything, SERVICE_OK);
        },
        error(code, message) { return Error(Anything, message, code); }
    );

    return Error(Anything, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

static Result(Anything) Service__complete_wait_syscall(ParkedOperationPtr parked_operation)
{
    match_result(ServiceEvent, Service__complete_wait(parked_operation),
        success(event)
        {
            return KernelContext__map_result(
                parked_operation->kernel_context,
                Success(Anything, (Anything) &event)
            );
        },
        error(code, message)
        {
            return KernelContext__map_result(
                parked_operation->kernel_context,
                Error(Anything, message, code)
            );
        }
    );

    return KernelContext__map_result(
        parked_operation->kernel_context,
        Error(Anything, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED)
    );
}

Result(SystemCallResult) Service__handler(
    KernelPtr kernel, Optional(ProcessPtr) process_opt, Anything service_operation,
    Anything arg2, Anything arg3, Anything arg4, Anything arg5
)
{

    match_optional(ProcessPtr, process_opt,
        some(process)
        {
            if (service_operation == SERVICE_BECOME_SERVICE)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_BECOME_SERVICE),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
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

                match_result(ServiceEvent, Scheduler__service_wait_with_context_ownership(
                    kernel->scheduler,
                    process,
                    kernel_context,
                    Some(Continuation, Service__complete_wait_syscall),
                    true
                ),
                    success(event)
                    {
                        Result(Anything) mapped = KernelContext__map_result(
                            kernel_context,
                            Success(Anything, (Anything) &event)
                        );
                        KernelContext__dispose_heap(kernel_context);
                        __d_printf(IODEV, "service wait return pid=%i code=%i\n", process->pid, SERVICE_OK);
                        match_result(Anything, mapped,
                            success(mapped_code) { return Success(SystemCallResult, mapped_code); },
                            error(mapped_code, mapped_message) { return Error(SystemCallResult, mapped_message, mapped_code); }
                        );
                    },
                    error(code, message)
                    {
                        if (code != SERVICE_EVENT_QUEUE_EMPTY)
                        {
                            KernelContext__dispose_heap(kernel_context);
                            __d_printf(IODEV, "service wait error pid=%i code=%i\n", process->pid, code);
                        }
                        return Error(SystemCallResult, message, code);
                    });
            }

            if (service_operation == SERVICE_MAP_MMIO)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_MAP_MMIO),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceMappedMemoryRequestPtr request = (ServiceMappedMemoryRequestPtr) arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(UserAddress, Process__map_mmio(process, request->physical_address, request->size),
                    success(address)
                    {
                        request->driver_address = address.virtual_addr;
                        request->device_address = request->physical_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_MAP_FAILED); }
                );
            }

            if (service_operation == SERVICE_DEVICE_ADDRESS)
            {
                ServiceDeviceAddressRequestPtr request = (ServiceDeviceAddressRequestPtr) arg2;
                if (request == NULL || request->driver_address == 0)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(Address, Process__device_address(process, request->driver_address),
                    success(device_address)
                    {
                        __d_printf(
                            IODEV,
                            "service device address pid=%i driver=0x%x device=0x%x\n",
                            (int) process->pid,
                            (unsigned int) request->driver_address,
                            (unsigned int) device_address
                        );
                        request->device_address = device_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_INVALID_BUFFER); }
                );
            }

            if (service_operation == SERVICE_SUBSCRIBE_IRQ)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_SUBSCRIBE_IRQ),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                uint64_t irq = (uint64_t) arg2;
                __d_printf(IODEV, "service subscribe irq pid=%i irq=%i\n", process->pid, (int) irq);
                match_result(IRQRegistrationStatus, IRQDispatcher__register_service(kernel->irq_dispatcher, irq, process),
                    success(_) { return Success(SystemCallResult, SERVICE_OK); },
                    error(code, message) { return Error(SystemCallResult, message, Service__map_irq_registration_status(code)); }
                );
            }

            if (service_operation == SERVICE_REGISTER_BLOCK_DEVICE)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_REGISTER_BLOCK_DEVICE),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceBlockDeviceRegistrationPtr request = (ServiceBlockDeviceRegistrationPtr) arg2;
                if (request == NULL || request->block_size != BLOCK_DEVICE_BLOCK_SIZE)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(BlockDeviceCode, BlockDeviceRegistry__register(
                    request->path, process, request->block_count, kernel->message_broker
                ),
                    success(_)
                    {
                        match_optional(FilesystemPtr, kernel->io_subsystem->root_filesystem,
                            some(root_fs) { FilesystemTable__mount_available_entries(root_fs); },
                            none_do_nothing()
                        );
                        return Success(SystemCallResult, SERVICE_OK);
                    }, error(code, message) { return Error(SystemCallResult, message, Service__map_block_registration_status(code)); });
            }

            if (service_operation == SERVICE_BLOCK_GET_REQUEST)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_HANDLE_BLOCK_REQUESTS),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                uint64_t request_id = (uint64_t) arg2;
                ServiceBlockRequestPtr target = (ServiceBlockRequestPtr) arg3;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(ServiceBlockRequest, BlockDeviceRegistry__get_request(process, request_id),
                    success(request)
                    {
                        *target = request;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_READ_DEVICE)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr) arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContext kernel_context = KernelContext__new(
                    Some(ProcessID, process->pid),
                    Some(SchedulerPtr, kernel->scheduler)
                );
                BlockDevice__set_current_context(&kernel_context);
                Result(BlockCount) read_result = BlockDeviceRegistry__read_from_process(
                    process,
                    request->device_path,
                    request->block,
                    request->block_count,
                    ptr(request->buffer, Data),
                    request->buffer_size,
                    kernel->message_broker
                );
                BlockDevice__clear_current_context();

                match_result(BlockCount, read_result,
                    success(blocks) { return Success(SystemCallResult, blocks); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_WRITE_DEVICE)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr) arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContext kernel_context = KernelContext__new(
                    Some(ProcessID, process->pid),
                    Some(SchedulerPtr, kernel->scheduler)
                );
                BlockDevice__set_current_context(&kernel_context);
                Result(BlockCount) write_result = BlockDeviceRegistry__write_from_process(
                    process,
                    request->device_path,
                    request->block,
                    request->block_count,
                    ptr(request->buffer, Data),
                    request->buffer_size,
                    kernel->message_broker
                );
                BlockDevice__clear_current_context();

                match_result(BlockCount, write_result,
                    success(blocks) { return Success(SystemCallResult, blocks); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_COMPLETE)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_HANDLE_BLOCK_REQUESTS),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceBlockCompletionPtr completion = (ServiceBlockCompletionPtr) arg2;
                if (completion == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(ServiceCode, BlockDeviceService__complete(process, completion),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_REGISTER_FILESYSTEM)
            {
                match_result(SystemCallResult, Service__require_capability(process, PROCESS_CAP_MOUNT_FILESYSTEM),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceFilesystemRegistrationPtr request = (ServiceFilesystemRegistrationPtr) arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_optional(FilesystemPtr, kernel->io_subsystem->root_filesystem,
                    some(root_filesystem)
                    {
                        match_result(ServiceCode, UserspaceFilesystemRegistry__register(
                            UserspaceFilesystemRegistry__shared(),
                            process,
                            request,
                            root_filesystem,
                            kernel->message_broker
                        ),
                            success(code) { return Success(SystemCallResult, code); },
                            error(code, message) { return Error(SystemCallResult, message, code); }
                        );
                    },
                    none_do_nothing()
                );

                return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
            }

            if (service_operation == SERVICE_FILESYSTEM_GET_REQUEST)
            {
                uint64_t request_id = (uint64_t) arg2;
                ServiceFilesystemRequestPtr target = (ServiceFilesystemRequestPtr) arg3;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(ServiceFilesystemRequest, UserspaceFilesystemRegistry__get_request(
                    UserspaceFilesystemRegistry__shared(), process, request_id
                ),
                    success(request)
                    {
                        *target = request;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_FILESYSTEM_COMPLETE)
            {
                ServiceFilesystemCompletionPtr completion = (ServiceFilesystemCompletionPtr) arg2;
                if (completion == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(ServiceCode, UserspaceFilesystemRegistry__complete(
                    UserspaceFilesystemRegistry__shared(), process, completion
                ),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_PUBLISH_READY)
            {
                if (!ProcessClass__is_service(process->process_class))
                {
                    return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS);
                }

                ServiceReadyRequestPtr request = (ServiceReadyRequestPtr) arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(ServiceCode, Scheduler__service_publish_ready(kernel->scheduler, &request->name),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_WAIT_READY)
            {
                ServiceReadyRequestPtr request = (ServiceReadyRequestPtr) arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContextPtr kernel_context = KernelContext__heap_new(
                    Some(ProcessID, process->pid),
                    Some(SchedulerPtr, kernel->scheduler)
                );

                match_result(ServiceCode, Scheduler__service_wait_ready_with_context_ownership(
                    kernel->scheduler,
                    process,
                    &request->name,
                    kernel_context,
                    Some(Continuation, SystemCall__Complete),
                    true
                ),
                    success(code)
                    {
                        KernelContext__dispose_heap(kernel_context);
                        return Success(SystemCallResult, code);
                    },
                    error(code, message)
                    {
                        if (code != SERVICE_EVENT_QUEUE_EMPTY)
                        {
                            KernelContext__dispose_heap(kernel_context);
                        }
                        return Error(SystemCallResult, message, code);
                    });
            }
        },
        none() { return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS); }
    );

    return Error(SystemCallResult, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

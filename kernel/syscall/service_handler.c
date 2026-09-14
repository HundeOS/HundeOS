#include "service_handler.h"

#include "../kernel_core.h"
#include "../debug.h"
#include "../scheduler/kernel_context.h"
#include "../scheduler/parked.h"
#include "../syscall/deferred.h"
#include "../service/service.h"
#include "../service/service_directory.h"
#include "../subsystems/io/block/block_device.h"
#include "../subsystems/io/file/register.h"
#include "../subsystems/io/file/userspace_filesystem.h"
#include "../subsystems/io/socket/socket.h"

static BString no_process = bstr("No process for service syscall");
static BString unsupported_operation = bstr("Unsupported service operation");
static BString permission_denied = bstr("Permission denied");

#define SERVICE_GIC_DISTRIBUTOR_SIZE 0x10000ULL

typedef struct ServiceMappedMemoryRequest
{
    Address physical_address;
    size_t size;
    Address driver_address;
    Address device_address;
} ServiceMappedMemoryRequest;

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

static Result(Anything) ServiceEvent__copy_to_target(ResultPtr(Anything) result, Anything context)
{
    ServiceEventPtr target = ptr(context, ServiceEvent);

    match_result(
        *result,
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
    match_result(
        Service__complete_wait(parked_operation),
        success(event)
        {
            Result(Anything) mapped_result = Success(Anything, (Anything)&event);
            return KernelContext__map_result(parked_operation->kernel_context, &mapped_result);
        },
        error(code, message)
        {
            Result(Anything) mapped_result = Error(Anything, message, code);
            return KernelContext__map_result(parked_operation->kernel_context, &mapped_result);
        }
    );

    Result(Anything) mapped_result = Error(Anything, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
    return KernelContext__map_result(parked_operation->kernel_context, &mapped_result);
}

Result(SystemCallResult)
    Service__handler(KernelPtr kernel, OptionalPtr(ProcessPtr) process_opt, Anything service_operation, Anything arg2,
                     Anything arg3, Anything arg4, Anything arg5)
{

    match_optional(
        *process_opt,
        some(process)
        {
            if (service_operation == SERVICE_QUEUE_LISTEN)
            {
                match_result(
                    Scheduler__queue_listen(kernel->scheduler, process, arg2, arg3),
                    success(value) { return Success(SystemCallResult, value); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }
            if (service_operation == SERVICE_QUEUE_CONNECT || service_operation == SERVICE_QUEUE_ACCEPT ||
                service_operation == SERVICE_QUEUE_CLOSE)
            {
                match_result(
                    Scheduler__queue_operation(kernel->scheduler, process, service_operation, arg2),
                    success(value) { return Success(SystemCallResult, value); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }
            if (service_operation == SERVICE_BECOME_SERVICE)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_BECOME_SERVICE),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                process->process_class = PROCESS_CLASS_SERVICE;
                return Success(SystemCallResult, SERVICE_OK);
            }

            if (service_operation == SERVICE_WAIT)
            {
                ServiceEventPtr target = (ServiceEventPtr)arg2;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                __d_printf(IODEV, "service wait pid=%i name=%s queued=%i target=%p\n", process->pid,
                           cstr(process->name), (int)ServiceEvents__len(&process->service.events), arg2);

                KernelContextPtr kernel_context = KernelContext__heap_new_with_mapper(
                    &Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler),
                    ServiceEvent__copy_to_target, (Anything)target);

                match_result(
                    Scheduler__service_wait_with_context_ownership(kernel->scheduler, process, kernel_context,
                                                                   &Some(Continuation, Service__complete_wait_syscall),
                                                                   true),
                    success(event)
                    {
                        Result(Anything) mapped =
                            KernelContext__map_result(kernel_context, &Success(Anything, (Anything)&event));
                        KernelContext__dispose_heap(kernel_context);
                        __d_printf(IODEV, "service wait return pid=%i code=%i\n", process->pid, SERVICE_OK);
                        match_result(
                            mapped,
                            success(mapped_code) { return Success(SystemCallResult, mapped_code); },
                            error(mapped_code, mapped_message)
                            {
                                return Error(SystemCallResult, mapped_message, mapped_code);
                            }
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
                    }
                );
            }

            if (service_operation == SERVICE_POLL)
            {
                ServiceEventPtr target = (ServiceEventPtr)arg2;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }
                match_result(
                    Scheduler__service_take_next_event(kernel->scheduler, process),
                    success(event)
                    {
                        *target = event;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_MAP_MMIO)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_MAP_MMIO),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceMappedMemoryRequestPtr request = (ServiceMappedMemoryRequestPtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    Process__map_mmio(process, request->physical_address, request->size),
                    success(address)
                    {
                        request->driver_address = address.virtual_addr;
                        request->device_address = request->physical_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_MAP_FAILED); }
                );
            }

            if (service_operation == SERVICE_CLAIM_VIRTIO_DEVICE)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_MAP_MMIO),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                match_result(
                    Process__claim_virtio_device(process, (Address)arg2),
                    success(code) { return Success(SystemCallResult, SERVICE_OK); },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_INVALID_BUFFER); }
                );
            }
            if (service_operation == SERVICE_DEVICE_ADDRESS)
            {
                ServiceDeviceAddressRequestPtr request = (ServiceDeviceAddressRequestPtr)arg2;
                if (request == NULL || request->driver_address == 0)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    Process__device_address(process, request->driver_address),
                    success(device_address)
                    {
                        __d_printf(IODEV, "service device address pid=%i driver=0x%x device=0x%x\n", (int)process->pid,
                                   (unsigned int)request->driver_address, (unsigned int)device_address);
                        request->device_address = device_address;
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_INVALID_BUFFER); }
                );
            }

            if (service_operation == SERVICE_SUBSCRIBE_IRQ)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_SUBSCRIBE_IRQ),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                uint64_t irq = (uint64_t)arg2;
                __d_printf(IODEV, "service subscribe irq pid=%i irq=%i\n", process->pid, (int)irq);
                match_result(
                    IRQDispatcher__register_service(kernel->irq_dispatcher, irq, process),
                    success(_) { return Success(SystemCallResult, SERVICE_OK); },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, Service__map_irq_registration_status(code));
                    }
                );
            }

            if (service_operation == SERVICE_REGISTER_INTERRUPT_MANAGER)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_SUBSCRIBE_IRQ),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceInterruptManagerRegistrationPtr request = (ServiceInterruptManagerRegistrationPtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }
                match_result(
                    IRQDispatcher__register_manager(kernel->irq_dispatcher, process),
                    success(queue_address)
                    {
                        match_optional(
                            kernel->system_info,
                            some(system_info)
                            {
                                match_result(
                                    Process__map_mmio(process,
                                                      system_info.interrupt_controller.distributor_physical_base,
                                                      SERVICE_GIC_DISTRIBUTOR_SIZE),
                                    success(distributor)
                                    {
                                        request->event_queue = queue_address;
                                        request->distributor = distributor.virtual_addr;
                                        return Success(SystemCallResult, SERVICE_OK);
                                    },
                                    error(code, message)
                                    {
                                        IRQDispatcher__dispose_process_resources(kernel->irq_dispatcher, process);
                                        return Error(SystemCallResult, message, SERVICE_MAP_FAILED);
                                    }
                                );
                            },
                            none_do_nothing()
                        );
                        IRQDispatcher__dispose_process_resources(kernel->irq_dispatcher, process);
                        return Error(SystemCallResult, unsupported_operation, SERVICE_MAP_FAILED);
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_ROUTE_MANAGED_IRQ)
            {
                ServiceManagedIRQRoutePtr request = (ServiceManagedIRQRoutePtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }
                match_result(
                    Scheduler__require_process_capability(kernel->scheduler, request->client_pid,
                                                          PROCESS_CAP_SUBSCRIBE_IRQ),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                critical_section(kernel->scheduler->scheduler_lock, {
                    match_result(
                        IRQDispatcher__register_managed_irq(kernel->irq_dispatcher, request->irq, process),
                        success(_) { cs_return Success(SystemCallResult, SERVICE_OK); },
                        error(code, message)
                        {
                            cs_return Error(SystemCallResult, message, Service__map_irq_registration_status(code));
                        }
                    );
                });
            }

            if (service_operation == SERVICE_REGISTER_BLOCK_DEVICE)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_REGISTER_BLOCK_DEVICE),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceBlockDeviceRegistrationPtr request = (ServiceBlockDeviceRegistrationPtr)arg2;
                if (request == NULL || request->block_size != BLOCK_DEVICE_BLOCK_SIZE)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__register(&request->path, process, request->block_count,
                                                  kernel->message_broker),
                    success(_)
                    {
                        match_optional(
                            kernel->io_subsystem->root_filesystem,
                            some(root_fs) { FilesystemTable__mount_available_entries(root_fs); },
                            none_do_nothing()
                        );
                        return Success(SystemCallResult, SERVICE_OK);
                    },
                    error(code, message)
                    {
                        return Error(SystemCallResult, message, Service__map_block_registration_status(code));
                    }
                );
            }

            if (service_operation == SERVICE_BLOCK_GET_REQUEST)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_HANDLE_BLOCK_REQUESTS),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                uint64_t request_id = (uint64_t)arg2;
                ServiceBlockRequestPtr target = (ServiceBlockRequestPtr)arg3;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__get_request(process, request_id),
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
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr)arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContext kernel_context =
                    KernelContext__new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));
                BlockDevice__set_current_context(&kernel_context);
                Result(BlockCount) read_result = BlockDeviceRegistry__read_from_process(
                    process, &request->device_path, request->block, request->block_count, ptr(request->buffer, Data),
                    request->buffer_size, kernel->message_broker);
                BlockDevice__clear_current_context();

                match_result(
                    read_result,
                    success(blocks) { return Success(SystemCallResult, blocks); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_AUTHORIZE_BLOCK_CLIENT)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_HANDLE_BLOCK_REQUESTS),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                match_result(
                    Scheduler__require_process_capability(kernel->scheduler, arg2, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success(code) { return Success(SystemCallResult, SERVICE_OK); },
                    error(code, message) { return Error(SystemCallResult, message, SERVICE_PERMISSION_DENIED); }
                );
            }

            if (service_operation == SERVICE_BLOCK_CONNECT)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockConnectionRequestPtr request = (ServiceBlockConnectionRequestPtr)arg2;
                if (request == NULL) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__connect(process, request, kernel->message_broker),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_DISCONNECT)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockDisconnectionRequestPtr request = (ServiceBlockDisconnectionRequestPtr)arg2;
                if (request == NULL) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__disconnect(process, request),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_WRITE_DEVICE)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr)arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContext kernel_context =
                    KernelContext__new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));
                BlockDevice__set_current_context(&kernel_context);
                Result(BlockCount) write_result = BlockDeviceRegistry__write_from_process(
                    process, &request->device_path, request->block, request->block_count, ptr(request->buffer, Data),
                    request->buffer_size, kernel->message_broker);
                BlockDevice__clear_current_context();

                match_result(
                    write_result,
                    success(blocks) { return Success(SystemCallResult, blocks); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_READ_DEVICE_ASYNC)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr)arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__read_from_process_async(process, &request->device_path, request->block,
                                                                 request->block_count, ptr(request->buffer, Data),
                                                                 request->buffer_size, kernel->message_broker),
                    success(request_id) { return Success(SystemCallResult, request_id); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_WRITE_DEVICE_ASYNC)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_USE_BLOCK_DEVICES),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceBlockReadRequestPtr request = (ServiceBlockReadRequestPtr)arg2;
                if (request == NULL || request->buffer == 0) // hundelint: ignore-no-null
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceRegistry__write_from_process_async(process, &request->device_path, request->block,
                                                                  request->block_count, ptr(request->buffer, Data),
                                                                  request->buffer_size, kernel->message_broker),
                    success(request_id) { return Success(SystemCallResult, request_id); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_BLOCK_COMPLETE)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_HANDLE_BLOCK_REQUESTS),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
                ServiceBlockCompletionPtr completion = (ServiceBlockCompletionPtr)arg2;
                if (completion == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    BlockDeviceService__complete(process, completion),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_REGISTER_FILESYSTEM)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_MOUNT_FILESYSTEM),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                ServiceFilesystemRegistrationPtr request = (ServiceFilesystemRegistrationPtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_optional(
                    kernel->io_subsystem->root_filesystem,
                    some(root_filesystem)
                    {
                        match_result(
                            UserspaceFilesystemRegistry__register(UserspaceFilesystemRegistry__shared(), process,
                                                                  request, root_filesystem, kernel->message_broker),
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
                uint64_t request_id = (uint64_t)arg2;
                ServiceFilesystemRequestPtr target = (ServiceFilesystemRequestPtr)arg3;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    UserspaceFilesystemRegistry__get_request(UserspaceFilesystemRegistry__shared(), process,
                                                             request_id),
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
                ServiceFilesystemCompletionPtr completion = (ServiceFilesystemCompletionPtr)arg2;
                if (completion == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    UserspaceFilesystemRegistry__complete(UserspaceFilesystemRegistry__shared(), process, completion),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_REGISTER_SOCKET_ROUTER)
            {
                match_result(
                    Service__require_capability(process, PROCESS_CAP_BECOME_SERVICE),
                    success_do_nothing(),
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );

                match_result(
                    SocketIO__registered_registry(kernel->io_subsystem),
                    success(registry)
                    {
                        match_result(
                            SocketRegistry__register_router(registry, process, kernel->message_broker),
                            success(code) { return Success(SystemCallResult, code); },
                            error(code, message) { return Error(SystemCallResult, message, code); }
                        );
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_SOCKET_GET_REQUEST)
            {
                uint64_t request_id = (uint64_t)arg2;
                IOSocketRouterRequestPtr target = (IOSocketRouterRequestPtr)arg3;
                if (target == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    SocketIO__registered_registry(kernel->io_subsystem),
                    success(registry)
                    {
                        match_result(
                            SocketRegistry__get_request(registry, process, request_id),
                            success(request)
                            {
                                *target = request;
                                return Success(SystemCallResult, SERVICE_OK);
                            },
                            error(code, message) { return Error(SystemCallResult, message, code); }
                        );
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_SOCKET_COMPLETE)
            {
                IOSocketRouterCompletionPtr completion = (IOSocketRouterCompletionPtr)arg2;
                if (completion == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    SocketIO__registered_registry(kernel->io_subsystem),
                    success(registry)
                    {
                        match_result(
                            SocketRegistry__complete(registry, process, completion),
                            success(code) { return Success(SystemCallResult, code); },
                            error(code, message) { return Error(SystemCallResult, message, code); }
                        );
                    },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_PUBLISH_READY)
            {
                if (!ProcessClass__is_service(process->process_class))
                {
                    return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS);
                }

                ServiceReadyRequestPtr request = (ServiceReadyRequestPtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                match_result(
                    Scheduler__service_publish_ready(kernel->scheduler, process, &request->name),
                    success(code) { return Success(SystemCallResult, code); },
                    error(code, message) { return Error(SystemCallResult, message, code); }
                );
            }

            if (service_operation == SERVICE_WAIT_READY)
            {
                ServiceReadyRequestPtr request = (ServiceReadyRequestPtr)arg2;
                if (request == NULL)
                {
                    return Error(SystemCallResult, unsupported_operation, SERVICE_INVALID_BUFFER);
                }

                KernelContextPtr kernel_context =
                    KernelContext__heap_new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));

                match_result(
                    Scheduler__service_wait_ready_with_context_ownership(
                        kernel->scheduler, process, &request->name, kernel_context,
                        &Some(Continuation, SystemCall__Complete), true),
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
                    }
                );
            }
        },
        none() { return Error(SystemCallResult, no_process, SERVICE_NOT_SERVICE_PROCESS); }
    );

    return Error(SystemCallResult, unsupported_operation, SERVICE_OPERATION_NOT_SUPPORTED);
}

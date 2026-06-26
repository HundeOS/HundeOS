#include "io_subsystem.h"

#include "kernel_core.h"
#include "process.h"
#include "kalloc.h"
#include "arch/page_table.h"
#include "file/file_io.h"
#include "../../types/result.h"
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../debug.h"
#include "scheduler/scheduler.h"
#include "syscalls/io.h"
#include "../message/subsystem_codes.h"

FixedArrayBodyDefine(PIDHandles);
MapBodyDefine(ProcessHandleMap);
MapBodyDefine(IOServerProcessMap);
MapBodyDefine(IOServerRoleMap);


static KString no_device_found_format = KSTRING_NEW("No device found %s\n");
static KString failed_to_register_format = KSTRING_NEW("Failed to register device %s\n");
static KString unknown_handle_format = KSTRING_NEW("Unknown handle %i\n");
static KString no_handle_supplied = KSTRING_NEW("No handle supplied");
static KString unreachable_code = KSTRING_NEW("IO unreachable code path");
static KString no_buffer_supplied = KSTRING_NEW("No buffer supplied");
static KString no_pid_found = KSTRING_NEW("No pid found");

DeviceHandle _to_device_handle(ArrayIndex index)
{
    return (index + 1) & 0xFFFFFFFF;
}

IOInternalHandle device_internal_handle(IOHandle handle)
{
    return handle & 0xFFFFFFFF;
}

static DeviceHandle device_handle(IOHandle handle)
{
    return (DeviceHandle) ((handle >> 32) & 0xFFFFFFFF);
}

Result(DeviceHandle) IOSubsystem__register_device(
    IOSubsystemPtr self, KString path, IOPtr device, IOStatePtr device_state
)
{
    DeviceRegistration reg = (DeviceRegistration) {
        .device = device,
        .path = path,
        .state = device_state
    };

    Result(CollectionCode) index_res = IODevicesArray__push(&self->devices, reg);

    ResultMatch(CollectionCode, index_res);
        IsError(code, message,
        {
            return Error(DeviceHandle, message, code);
        });
        IsSuccess(index,
        {
            return Success(DeviceHandle, _to_device_handle(index));
        });
    End


    return Error(DeviceHandle, KString__sprintf(&failed_to_register_format, path), IO_REGISTER_FAILED);
}

bool path_match(DeviceRegistration * device, void * v_path)
{
    KStringPtr path = v_path;
    __d_printf(IOSUB, "Checking path %s %s\n", cstr_p(path), cstr(device->path));
    return KString__starts_with(path, &device->path);
}

static FindResultOptional(IODevicesArray) IOSubsystem__find_device(IOSubsystemPtr self, KStringPtr path)
{
    return IODevicesArray__find(&self->devices, path_match, path);
}

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, IOContextPtr context, KString path, Flags flags)
{
    __d_printf(IOSUB, "Opening %p\n", self);
    Result(IOHandle) error = Error(IOHandle, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);

    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            __d_printf(IOSUB, "Opening device %p %p %i\n", match.value->device, match.value->device->open, match.index);
            DeviceRegistrationPtr registration = match.value;
            Result(IOInternalHandle) open_result = registration->device->open(registration->state, path, flags);
            __d_printf(IOSUB, "Opened device %p %i\n", match.value->device, match.index);

            ResultMatch(IOInternalHandle, open_result);
                IsSuccess(handle,
                {
                    IOHandle external_handle = (IOHandle) handle | (((IOHandle)_to_device_handle(match.index)) << 32);
                    IOHandlesMap__set(&self->handles, external_handle, registration);
                    Match(ProcessID, context->kernel_context.pid)
                        IsSome(pid,
                        {
                            MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&self->process_handles, pid))
                                IsNone(
                                {
                                    PIDHandles pid_handles = PIDHandles__new();
                                    PIDHandles__push(&pid_handles, external_handle);
                                    ProcessHandleMap__set(&self->process_handles, pid, pid_handles);
                                })
                                IsSome(handles,
                                {
                                    PIDHandles__push(handles, external_handle);
                                })
                            End
                        })
                        IsNone(
                        {
                            __d_println(PANIC, "TODO: No process found for open, must be kernel, should track its handle");
                        });
                    End
                    return Success(IOHandle, external_handle);
                });
                IsError(code, message, return Error(IOHandle, message, code))
            End
        });
    End

    return error;
}

static DirectoryEntry DirectoryEntry__from_node(FilesystemNodePtr node)
{
    DirectoryEntry entry = (DirectoryEntry) { 0 };
    entry.attributes = node->attribs;
    entry.name_len = node->name.len;

    size_t max_name = IO_DIRENT_NAME_MAX - 1;
    size_t copy_len = node->name.len < max_name ? node->name.len : max_name;
    for (size_t i = 0; i < copy_len; i++)
    {
        entry.name[i] = node->name.data[i];
    }
    entry.name[copy_len] = '\0';
    entry.name_len = copy_len;

    return entry;
}

Result(DirectoryEntryCount) IOSubsystem__list_dir(
    IOSubsystemPtr self, KString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    KString root_path = KSTRING_NEW("/");
    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            DeviceRegistrationPtr registration = match.value;
            if (!KString__equals(&registration->path, &root_path))
            {
                return Error(DirectoryEntryCount, kstring("Directory listing not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            ResultMatch(FilesystemNodes, file_io->list_nodes(registration->state, NULL, path))
                IsSuccess(nodes,
                {
                    DirectoryEntryCount count = 0;
                    FAIterate(FilesystemNodes, nodes, index, node)
                        if ((size_t) count >= max_entries)
                        {
                            return Success(DirectoryEntryCount, count);
                        }
                        entries[count] = DirectoryEntry__from_node(node);
                        count++;
                    End
                    return Success(DirectoryEntryCount, count);
                })
                IsError(code, message, return Error(DirectoryEntryCount, message, code))
            End
        });
    End

    return Error(DirectoryEntryCount, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__mkdir(IOSubsystemPtr self, KString path)
{
    KString root_path = KSTRING_NEW("/");
    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            DeviceRegistrationPtr registration = match.value;
            if (!KString__equals(&registration->path, &root_path))
            {
                return Error(IOStatus, kstring("mkdir not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            return file_io->mkdir(registration->state, path);
        });
    End

    return Error(IOStatus, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__remove(IOSubsystemPtr self, KString path, Flags flags)
{
    KString root_path = KSTRING_NEW("/");
    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            DeviceRegistrationPtr registration = match.value;
            if (!KString__equals(&registration->path, &root_path))
            {
                return Error(IOStatus, kstring("remove not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            return file_io->remove(registration->state, path, flags);
        });
    End

    return Error(IOStatus, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContextPtr context)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            __d_printf(IOSUB, "Closing handle %i\n", handle);
            CriticalSection(self->lock,
            {
                Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                    IsSome(registration,
                    {
                        Result(IOInternalHandle) close_res =
                            registration->device->close(registration->state, device_internal_handle(handle));

                        IOHandlesMap__delete(&self->handles, handle);
                        Match(ProcessID, context->kernel_context.pid)
                            IsSome(pid,
                            {
                                MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&self->process_handles, pid))
                                    IsSome(proc_handles,
                                    {
                                        PIDHandles__remove(proc_handles, handle);
                                    })
                                End
                            });
                            IsNone(
                            {
                                __d_println(PANIC, "TODO: No process found for close, must be kernel, should track its handle");
                            });
                        End

                        ResultMatch(IOInternalHandle, close_res)
                            IsSuccess(_, _cs_return Success(IOHandle, handle));
                            IsError(code, message, _cs_return Error(IOHandle, message, code));
                        End
                    });
                End
            });

            KString error_msg = KString__sprintf(&unknown_handle_format, handle) ;
            return Error(IOHandle, error_msg, IO_NOT_OPEN);
        })
        IsNone(
        {
            return Error(IOHandle, no_handle_supplied, IO_NOT_OPEN);
        })
    End
    return Error(IOHandle, unreachable_code, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) IOSubsystem__transfer(IOBufferPtr buffer, DataPtr result, size_t size)
{
    __d_printf(DEBUG, "reading %i %i\n", buffer->read_pos, buffer->write_pos);
    ResultMatch(ItemCount, IOBuffer__drain(buffer, result, size))
        IsSuccess(num, return Success(ByteCount, num));
        IsError(code, message, return Error(ByteCount, message, code));
    End
}

void IOSubsystem__data_arrived(IOSubsystemPtr self, IOContextPtr parked_context)
{
    __d_println(SCHEDULER, "Data arrived");
    __d_printf(IOSUB, "IO data arrived parked_context=%p\n", parked_context);
    Match(IOHandle, parked_context->handle)
        IsSome(handle,
        {
            __d_printf(IOSUB, "IO data arrived handle=0x%x\n", handle);
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(match,
                {
                    __d_printf(IOSUB, "IO data arrived device=%p state=%p\n", match->device, match->state);
                    match->device->unpark(match->state);
                    __d_println(IOSUB, "IO data arrived device unpark done");

                    Match(ProcessID, parked_context->kernel_context.pid)
                        IsSome(pid,
                        {
                            __d_println(IOSUB, "IO sub unparking process");
                            __d_printf(IOSUB, "IO data arrived parked pid=%i\n", pid);
                            MessagePtr message =
                                Message__heap_new_anything(SOURCE_IO, SUBSYS_IO_AVAILABLE, pid);

                            ResultMatch(AckCode, MessageBroker__deliver(self->message_broker, message));
                                IsSuccess(ack, __d_printf(IOSUB, "IO data arrived message delivered pid=%i ack=%i\n", pid, ack);)
                                IsError(code, message, __d_println(PANIC, cstr(message));)
                            End
                            __d_println(IOSUB, "IO sub unparked");
                        })
                        IsNone(__d_println(PANIC, "No pid found for parked context"));
                    End
                })
                IsNone(
                    __d_println(PANIC, "No device found for parking context");
                )
            End
        })
        IsNone(__d_println(PANIC, "No handle supplied"))
    End
}

void IOSubsystem__park(IOSubsystemPtr self, IOContextPtr io_context)
{
    __d_println(IOSUB, "Parking io");
    Match(IOHandle, io_context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(match,
                {
                    __d_println(IOSUB, "Parking context to device");
                    match->device->park(match->state, io_context);

                    DeviceHandle handle_device = device_handle(handle);
                    Match(ProcessPtr, IOServerProcessMap__get(&self->read_server_processes, handle_device))
                        IsSome(server_process,
                        {
                            ServiceEvent event;
                            event.type = SERVICE_EVENT_IO;
                            event.source = SERVICE_IO_READ_REQUEST;
                            event.data = handle_device;
                            Service__enqueue(self->scheduler, server_process, event);
                        })
                    End
                })
                IsNone(
                    __d_println(IOSUB, "No device found for parking context");
                )
            End
        })
        IsNone(__d_println(PANIC, "No handle supplied"))
    End
}

Result(Anything) IOSubsystem__continue_read(ParkedKernelContextPtr context)
{
    KernelContextPtr kernel_context = context->kernel_context;
    IOContextPtr io_context = (IOContextPtr) kernel_context;

    __d_printf(IOSUB, "\nContinuing read %p %p\n", io_context, io_context->io_subsystem);
    Match(IOSubsystemPtr, io_context->io_subsystem)
        IsSome(io_subsystem,
        {
            ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, io_context))
                IsSuccess(bytes, return Success(Anything, bytes));
                IsError(code, message, return Error(Anything, message, code));
            End
        })
        IsNone(return Error(Anything, kstring("no io subsystem found"), IO_PANIC))
    End

    return Error(Anything, kstring("unexpected code path"), IO_PANIC);
}

Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContextPtr context)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Optional(DeviceRegistrationPtr) device_opt = None(DeviceRegistrationPtr);

            CriticalSection(self->lock,
            {
                device_opt = IOHandlesMap__get(&self->handles, handle);
            })
            Match(DeviceRegistrationPtr, device_opt);
                IsSome(registration,
                {
                    Match(ExternalBuffer, context->buffer)
                        IsSome(ext_buffer,
                        {
                            Result(IOBufferPtr) read_res = registration->device->read(
                                registration->state, device_internal_handle(handle), ext_buffer.len
                            );
                            ResultMatch(IOBufferPtr, read_res)
                                IsSuccess(buffer,
                                {
                                    __d_printf(IOSUB, "reading %p %p\n", context->buffer, buffer);
                                    return IOSubsystem__transfer(buffer, ext_buffer.data, ext_buffer.len);
                                })
                                IsError(code, error_message,
                                {
                                    if (code == IO_WAIT_FOR_DATA)
                                    {
                                        __d_println(IOSUB, "Parking waiting for IO");

                                        IOSubsystem__park(self, context);
                                        Match(ProcessID, context->kernel_context.pid)
                                            IsSome(pid,
                                            {
                                                __d_println(IOSUB, "IO sub parking process");
                                                MessagePtr message =
                                                    Message__heap_new_deferred(
                                                        SOURCE_IO, SUBSYS_IO_PARK, pid,
                                                        &context->kernel_context, IOSubsystem__continue_read
                                                    );

                                                MessageBroker__deliver(self->message_broker, message);

                                            })
                                            IsNone(__d_println(PANIC, "No pid found for parked context"));
                                        End

                                    }
                                    return Error(ByteCount, error_message, code);
                                })
                            End
                        });
                    End
                })
                IsNone(__d_println(PANIC, "No device found for handle"));
            End
        })
        IsNone(
        {
            return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN);
        })
    End
    return Error(ByteCount, unreachable_code, IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContextPtr context)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    Match(ExternalBuffer, context->buffer)
                        IsSome(buffer,
                        {
                            Result(ByteCount) write_result = registration->device->write(
                                registration->state, device_internal_handle(handle), buffer.data, buffer.len
                            );
                            ResultMatch(ByteCount, write_result)
                                IsSuccess(count,
                                {
                                    Match(ProcessPtr, IOServerProcessMap__get(&self->write_server_processes, device_handle(handle)))
                                        IsSome(server_process,
                                        {
                                            ServiceEvent event;
                                            event.type = SERVICE_EVENT_IO;
                                            event.source = SERVICE_IO_WRITE_REQUEST;
                                            event.data = device_handle(handle);
                                            Service__enqueue(self->scheduler, server_process, event);
                                        })
                                    End
                                    return Success(ByteCount, count);
                                })
                                IsError(code, message, return Error(ByteCount, message, code))
                            End
                        });
                        IsNone(
                        {
                            return Error(ByteCount, no_buffer_supplied, IO_UNKNOWN_BUFFER);
                        });
                    End
                });
            End
        })
    End

    return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOHandle) IOSubsystem__server_register(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, KString path, IOServerRole role
)
{
    ResultMatch(IOHandle, IOSubsystem__open(self, context, path, IO_OPEN_WRITE))
        IsSuccess(handle,
        {
            if (role == IO_SERVER_WRITE)
            {
                IOServerProcessMap__set(&self->write_server_processes, device_handle(handle), process);
            }
            else
            {
                IOServerProcessMap__set(&self->read_server_processes, device_handle(handle), process);
            }
            IOServerRoleMap__set(&self->server_roles, handle, role);
            return Success(IOHandle, handle);
        })
        IsError(code, message, return Error(IOHandle, message, code))
    End

    return Error(IOHandle, no_handle_supplied, IO_NOT_OPEN);
}

static Address IOSubsystem__align_down_page(Address value)
{
    return value & ~(PAGE_SIZE - 1);
}

static Address IOSubsystem__align_up_page(Address value)
{
    return (value + PAGE_SIZE - 1) & ~(PAGE_SIZE - 1);
}

static Result(IOServerBuffer) IOSubsystem__map_server_buffer(ProcessPtr process, IOServerBuffer buffer)
{
    Address kernel_data = (Address) buffer.data;
    Address kernel_page = IOSubsystem__align_down_page(kernel_data);
    Address offset = kernel_data - kernel_page;
    size_t map_size = IOSubsystem__align_up_page(offset + buffer.capacity);
    Address user_page = process->allocation_next;

    UserAddress user_address = (UserAddress) {
        .virtual_addr = user_page,
        .kernel_addr = Kernel__from_virtual_address(kernel_page),
        .read = true,
        .write = true,
        .execute = false,
        .valid = true,
    };

    PageTable__map(v_ptr(process->page_table, PageTable), user_address, map_size);
    ResultMatch(ProcessMemoryCode, Process__record_user_allocation(process, user_address, map_size, USER_ALLOCATION_DYNAMIC))
        IsSuccess(_,
        {
            process->allocation_next += map_size;
            return Success(IOServerBuffer, ((IOServerBuffer) {
                .data = (IOServerBufferDataPtr) (user_page + offset),
                .capacity = buffer.capacity,
            }));
        })
        IsError(code, message, return Error(IOServerBuffer, message, code))
    End

    return Error(IOServerBuffer, no_buffer_supplied, IO_UNKNOWN_BUFFER);
}

Result(IOStatus) IOSubsystem__server_get_buffer(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, IOServerBufferPtr target
)
{
    if (target == NULL)
    {
        return Error(IOStatus, no_buffer_supplied, IO_UNKNOWN_BUFFER);
    }

    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    IOServerRole role = Unwrap(IOServerRole, IOServerRoleMap__get(&self->server_roles, handle), IO_SERVER_READ);
                    IO_server_buffer buffer_getter = registration->device->server_buffer;
                    if (role == IO_SERVER_WRITE)
                    {
                        buffer_getter = registration->device->server_write_buffer;
                    }

                    if (buffer_getter == NULL)
                    {
                        return Error(IOStatus, kstring("server buffer not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    ResultMatch(IOServerBuffer, buffer_getter(registration->state))
                        IsSuccess(kernel_buffer,
                        {
                            ResultMatch(IOServerBuffer, IOSubsystem__map_server_buffer(process, kernel_buffer))
                                IsSuccess(user_buffer,
                                {
                                    *target = user_buffer;
                                    return Success(IOStatus, IO_OK);
                                })
                                IsError(code, message, return Error(IOStatus, message, code))
                            End
                        })
                        IsError(code, message, return Error(IOStatus, message, code))
                    End
                });
            End
        })
    End

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_data_available(IOSubsystemPtr self, IOContextPtr context, size_t byte_count)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    if (registration->device->data_available == NULL)
                    {
                        return Error(IOStatus, kstring("data available not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    Match(IOContextPtr, registration->device->data_available(registration->state, byte_count))
                        IsSome(parked_context, IOSubsystem__data_arrived(self, parked_context))
                    End

                    return Success(IOStatus, IO_OK);
                });
            End
        })
    End

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_data_consumed(IOSubsystemPtr self, IOContextPtr context, size_t byte_count)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    if (registration->device->data_consumed == NULL)
                    {
                        return Error(IOStatus, kstring("data consumed not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }
                    return registration->device->data_consumed(registration->state, byte_count);
                });
            End
        })
    End

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_eof(IOSubsystemPtr self, IOContextPtr context)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    if (registration->device->eof == NULL)
                    {
                        return Error(IOStatus, kstring("eof not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    Match(IOContextPtr, registration->device->eof(registration->state))
                        IsSome(parked_context, IOSubsystem__data_arrived(self, parked_context))
                    End

                    return Success(IOStatus, IO_OK);
                });
            End
        })
    End

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(Anything) IOSubsystem__dispose_of_process_resources(IOSubsystemPtr self, IOContextPtr context)
{
    Match(ProcessID, context->kernel_context.pid)
        IsSome(pid,
        {
            // map get a copy of the list as the handles will be removed as we call close
            MapMatch(ProcessHandleMap, ProcessHandleMap__get(&self->process_handles, pid))
                IsSome(handles,
                {
                    FAIterate(PIDHandles, handles, index, handle)
                        IOContextPtr handle_context = IOContext__from_io_context(context, Some(IOHandle, handle));
                        IOSubsystem__close(self, handle_context);
                    End
                })
            End
            ProcessHandleMap__delete(&self->process_handles, pid);
            return Success(Anything, pid);
        })
        IsNone(__d_println(PANIC, "No PID found for close all"))
    End

    return Error(Anything, no_pid_found, IO_UNKNOWN_PID);

}

void IOSubsystem__message_received(KernelListenerPtr listener, MessagePtr message)
{
    if (message->source_code != PROC_STATE_DEAD)
    {
        return;
    }
    Match(Payload, message->payload)
        IsSome(payload,
        {
            if (payload.type == PAYLOAD_TYPE_ANYTHING)
            {
                ProcessID pid = payload.anything;
                IOContextPtr context = IOContext__from_message_listener(listener, pid);
                IOSubsystem__dispose_of_process_resources(listener->kernel->io_subsystem, context);
                IOContext__dispose(context);
            }
        })
    End
}


Result(ListenerRegisterResult) IOSubsystem__register_listeners(KernelPtr kernel)
{
    Listener listener = KernelListener__new(kernel, SOURCE_PROCESS, IOSubsystem__message_received);
    return MessageBroker__register_listener(kernel->message_broker, listener);
}

IOSubsystemPtr IOSubsystem__heap_new(SchedulerPtr scheduler, MessageBrokerPtr message_broker)
{
    IOSubsystemPtr io_subsystem = ptr(Kernel__alloc_kernel(sizeof(IOSubsystem)), IOSubsystem);
    io_subsystem->scheduler = scheduler;
    io_subsystem->message_broker = message_broker;

    MapInit(IOHandlesMap, io_subsystem->handles)
    MapInit(ProcessHandleMap, io_subsystem->process_handles);
    MapInit(IOServerProcessMap, io_subsystem->read_server_processes);
    MapInit(IOServerProcessMap, io_subsystem->write_server_processes);
    MapInit(IOServerRoleMap, io_subsystem->server_roles);
    ArrayInit(IODevicesArray, io_subsystem->devices);

    io_subsystem->lock = MUTEX_INIT;
    return io_subsystem;
}

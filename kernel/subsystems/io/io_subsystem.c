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
#include "../../service/service.h"
#include "syscalls/io.h"
#include "stream/stream.h"
#include "../message/io_operation_message.h"
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
static KString tty_path = KSTRING_NEW(":tty:");

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

static Result(ServiceCode) IOSubsystem__deliver_service_event(
    IOSubsystemPtr self, ProcessPtr process, ServiceEvent event
)
{
    MessagePtr message = Message__heap_new_service_event_delivery(
        SOURCE_SERVICE, SUBSYS_SERVICE_EVENT_AVAILABLE, process, event
    );

    ResultMatch(AckCode, MessageBroker__deliver(self->message_broker, message))
        IsSuccess(_, return Success(ServiceCode, SERVICE_OK))
        IsError(code, message, return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR))
    End

    return Error(ServiceCode, unreachable_code, SERVICE_SCHEDULER_ERROR);
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

Result(IOHandle) IOSubsystem__record_open_handle(
    IOSubsystemPtr self, IOContextPtr context, KString path, IOInternalHandle handle
)
{
    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            DeviceRegistrationPtr registration = match.value;
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
    End

    return Error(IOHandle, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
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
                IsSuccess(handle, return IOSubsystem__record_open_handle(self, context, path, handle));
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
            if (file_io->list_dir != NULL)
            {
                return file_io->list_dir(registration->state, path, entries, max_entries);
            }

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

Result(IOStatus) IOSubsystem__link(IOSubsystemPtr self, KString source_path, KString target_path)
{
    KString root_path = KSTRING_NEW("/");
    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &source_path));
        IsSome(source_match,
        {
            FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &target_path));
                IsSome(target_match,
                {
                    DeviceRegistrationPtr source_registration = source_match.value;
                    DeviceRegistrationPtr target_registration = target_match.value;
                    if (!KString__equals(&source_registration->path, &root_path)
                        || !KString__equals(&target_registration->path, &root_path))
                    {
                        return Error(IOStatus, kstring("link not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    FileIOPtr source_file_io = (FileIOPtr) source_registration->device;
                    FileIOPtr target_file_io = (FileIOPtr) target_registration->device;
                    if (source_file_io != target_file_io || source_registration->state != target_registration->state)
                    {
                        return Error(IOStatus, kstring("cross-filesystem link not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    return source_file_io->link(source_registration->state, source_path, target_path);
                });
            End
        });
    End

    return Error(IOStatus, KString__sprintf(&no_device_found_format, cstr(source_path)), IO_NO_DEVICE_FOUND);
}

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContextPtr context)
{
    Match(IOHandle, context->handle)
        IsSome(handle,
        {
            __d_printf(IOSUB, "IOSubsystem__close start handle=%i context=%p\n", handle, context);
            CriticalSection(self->lock,
            {
                __d_printf(IOSUB, "IOSubsystem__close locked handle=%i\n", handle);
                Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                    IsSome(registration,
                    {
                        __d_printf(
                            IOSUB,
                            "IOSubsystem__close calling device close handle=%i internal=%i registration=%p\n",
                            handle,
                            device_internal_handle(handle),
                            registration
                        );
                        Result(IOInternalHandle) close_res =
                            registration->device->close(registration->state, device_internal_handle(handle));
                        __d_printf(IOSUB, "IOSubsystem__close device close returned handle=%i\n", handle);

                        __d_printf(IOSUB, "IOSubsystem__close deleting handle=%i\n", handle);
                        IOHandlesMap__delete(&self->handles, handle);
                        Match(ProcessID, context->kernel_context.pid)
                            IsSome(pid,
                            {
                                __d_printf(IOSUB, "IOSubsystem__close removing pid handle pid=%i handle=%i\n", pid, handle);
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
                            IsSuccess(_,
                            {
                                __d_printf(IOSUB, "IOSubsystem__close success handle=%i\n", handle);
                                _cs_return Success(IOHandle, handle);
                            });
                            IsError(code, message,
                            {
                                __d_printf(IOSUB, "IOSubsystem__close device error handle=%i code=%i\n", handle, code);
                                _cs_return Error(IOHandle, message, code);
                            });
                        End
                    });
                End
            });

            __d_printf(IOSUB, "IOSubsystem__close unknown handle=%i\n", handle);
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

Result(IOStatus) IOSubsystem__data_arrived(IOSubsystemPtr self, IOContextPtr parked_context)
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
                    ResultMatch(ParkStatus, match->device->unpark(match->state))
                        IsError(code, message, return Error(IOStatus, message, code))
                    End
                    __d_println(IOSUB, "IO data arrived device unpark done");

                    Match(ProcessID, parked_context->kernel_context.pid)
                        IsSome(pid,
                        {
                            __d_println(IOSUB, "IO sub unparking process");
                            __d_printf(IOSUB, "IO data arrived parked pid=%i handle=%i\n", pid, handle);
                            MessagePtr message = Message__heap_new_io_operation(
                                SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, handle
                            );

                            ResultMatch(AckCode, MessageBroker__deliver(self->message_broker, message));
                                IsSuccess(ack,
                                {
                                    __d_printf(IOSUB, "IO data arrived message delivered pid=%i ack=%i\n", pid, ack);
                                    __d_println(IOSUB, "IO sub unparked");
                                    return Success(IOStatus, IO_OK);
                                })
                                IsError(code, message, return Error(IOStatus, message, code))
                            End
                        })
                        IsNone(return Error(IOStatus, no_pid_found, IO_UNKNOWN_PID));
                    End
                })
                IsNone(
                    return Error(IOStatus, kstring("No device found for parking context"), IO_NOT_OPEN);
                )
            End
        })
        IsNone(return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN))
    End

    return Error(IOStatus, unreachable_code, IO_PANIC);
}

static Result(ParkStatus) IOSubsystem__park(IOSubsystemPtr self, IOContextPtr io_context)
{
    __d_println(IOSUB, "Parking io");
    Match(IOHandle, io_context->handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(match,
                {
                    __d_println(IOSUB, "Parking context to device");
                    ResultMatch(ParkStatus, match->device->park(match->state, io_context))
                        IsError(code, message, return Error(ParkStatus, message, code))
                    End

                    DeviceHandle handle_device = device_handle(handle);
                    Match(ProcessPtr, IOServerProcessMap__get(&self->read_server_processes, handle_device))
                        IsSome(server_process,
                        {
                            ServiceEvent event;
                            event.type = SERVICE_EVENT_IO;
                            event.source = SERVICE_IO_READ_REQUEST;
                            event.data = handle_device;
                            ResultMatch(ServiceCode, IOSubsystem__deliver_service_event(self, server_process, event))
                                IsError(code, message, __d_printf(PANIC, "Failed to deliver IO read service event: %s", cstr(message)))
                            End
                        })
                    End

                    return Success(ParkStatus, IO_PARK_OK);
                })
                IsNone(
                    return Error(ParkStatus, kstring("No device found for parking context"), IO_NOT_OPEN);
                )
            End
        })
        IsNone(return Error(ParkStatus, no_handle_supplied, IO_NOT_OPEN))
    End

    return Error(ParkStatus, unreachable_code, IO_NOT_OPEN);
}

Result(Anything) IOSubsystem__continue_read(ParkedOperationPtr parked_operation)
{
    KernelContextPtr kernel_context = parked_operation->kernel_context;
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

static Result(ByteCount) IOSubsystem__request_read_park(IOSubsystemPtr self, IOContextPtr context)
{
    __d_println(IOSUB, "Parking waiting for IO");

    IOContextPtr parked_context = IOContext__from_io_context(context, context->handle);

    ResultMatch(ParkStatus, IOSubsystem__park(self, parked_context))
        IsError(code, message,
        {
            IOContext__dispose(parked_context);
            return Error(ByteCount, message, code);
        })
    End

    Match(ProcessID, parked_context->kernel_context.pid)
        IsSome(pid,
        {
            Match(IOHandle, parked_context->handle)
                IsSome(handle,
                {
                    __d_println(IOSUB, "IO sub parking process");
                    MessagePtr message = Message__heap_new_deferred_with_context_ownership_and_operation_key(
                        SOURCE_IO,
                        SUBSYS_IO_PARK,
                        pid,
                        &parked_context->kernel_context,
                        IOSubsystem__continue_read,
                        true,
                        Some(OperationKey, handle)
                    );

                    ResultMatch(AckCode, MessageBroker__deliver(self->message_broker, message))
                        IsSuccess(_, return Error(ByteCount, kstring("IO read parked"), IO_WAIT_FOR_DATA))
                        IsError(code, message, return Error(ByteCount, message, code))
                    End
                })
                IsNone(return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN))
            End
        })
        IsNone(return Error(ByteCount, no_pid_found, IO_UNKNOWN_PID))
    End

    return Error(ByteCount, unreachable_code, IO_PANIC);
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
                                registration->state, device_internal_handle(handle), ext_buffer.len, ext_buffer
                            );
                            ResultMatch(IOBufferPtr, read_res)
                                IsSuccess(buffer,
                                {
                                    __d_printf(IOSUB, "reading %p %p\n", context->buffer, buffer);
                                    ResultMatch(ByteCount, IOSubsystem__transfer(buffer, ext_buffer.data, ext_buffer.len))
                                        IsSuccess(bytes,
                                        {
                                            if (bytes > 0
                                                && registration->state == (IOStatePtr) StreamState__instance()
                                                && KString__equals(&registration->path, &tty_path))
                                            {
                                                __d_printf(
                                                    IODEV,
                                                    "IOSubsystem__read tty first_byte=%i first_byte_hex=%x bytes=%i\n",
                                                    (int) ((unsigned char) ext_buffer.data[0]),
                                                    (unsigned int) ((unsigned char) ext_buffer.data[0]),
                                                    (int) bytes
                                                );
                                            }
                                            return Success(ByteCount, bytes);
                                        })
                                        IsError(code, message, return Error(ByteCount, message, code))
                                    End
                                })
                                IsError(code, error_message,
                                {
                                    if (code == IO_WAIT_FOR_DATA && registration->device->park != NULL)
                                    {
                                        return IOSubsystem__request_read_park(self, context);
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
                                            ResultMatch(ServiceCode, IOSubsystem__deliver_service_event(self, server_process, event))
                                                IsError(code, message, __d_printf(PANIC, "Failed to deliver IO write service event: %s", cstr(message)))
                                            End
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
                        IsSome(parked_context,
                        {
                            ResultMatch(IOStatus, IOSubsystem__data_arrived(self, parked_context))
                                IsSuccess(status, return Success(IOStatus, status))
                                IsError(code, message, return Error(IOStatus, message, code))
                            End
                        })
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
                        IsSome(parked_context,
                        {
                            ResultMatch(IOStatus, IOSubsystem__data_arrived(self, parked_context))
                                IsSuccess(status, return Success(IOStatus, status))
                                IsError(code, message, return Error(IOStatus, message, code))
                            End
                        })
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
            if (payload.type == PAYLOAD_TYPE_PROCESS_ID)
            {
                ProcessID pid = payload.process_id;
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

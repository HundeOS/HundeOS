#include "io_subsystem.h"

#include "kernel_core.h"
#include "kernel_init.h"
#include "process.h"
#include "kalloc.h"
#include "arch/page_table.h"
#include "file/register.h"
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


static BString no_device_found_format = BSTRING_NEW("No device found %s\n");
static BString failed_to_register_format = BSTRING_NEW("Failed to register device %s\n");
static BString unknown_handle_format = BSTRING_NEW("Unknown handle %i\n");
static BString no_handle_supplied = BSTRING_NEW("No handle supplied");
static BString unreachable_code = BSTRING_NEW("IO unreachable code path");
static BString no_buffer_supplied = BSTRING_NEW("No buffer supplied");
static BString no_pid_found = BSTRING_NEW("No pid found");
static BString tty_path = BSTRING_NEW(":tty:");

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

    match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
        success(_) { return Success(ServiceCode, SERVICE_OK); },
        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
    )

    return Error(ServiceCode, unreachable_code, SERVICE_SCHEDULER_ERROR);
}

Result(DeviceHandle) IOSubsystem__register_device(
    IOSubsystemPtr self, BString path, IOPtr device, IOStatePtr device_state
)
{
    DeviceRegistration reg = (DeviceRegistration) {
        .device = device,
        .path = path,
        .state = device_state
    };

    Result(CollectionCode) index_res = IODevicesArray__push(&self->devices, reg);

    match_result(CollectionCode, index_res,
        success(index)
        {
            return Success(DeviceHandle, _to_device_handle(index));
        },
        error(code, message)
        {
            return Error(DeviceHandle, message, code);
        }
    );


    return Error(DeviceHandle, BString__sprintf(&failed_to_register_format, path), IO_REGISTER_FAILED);
}

bool path_match(DeviceRegistration * device, void * v_path)
{
    BStringPtr path = v_path;
    __d_printf(IOSUB, "Checking path %s %s\n", cstr_p(path), cstr(device->path));
    return BString__starts_with(path, &device->path);
}

static FindResultOptional(IODevicesArray) IOSubsystem__find_device(IOSubsystemPtr self, BStringPtr path)
{
    return IODevicesArray__find(&self->devices, path_match, path);
}

static bool IOSubsystem__is_dev_path(ConstBStringPtr path)
{
    BString dev_prefix = BSTRING_NEW("/dev/");
    return BString__starts_with(path, &dev_prefix);
}

static BString IOSubsystem__dev_name_from_path(BString path)
{
    BStringArray segments = BString__split(&path, '/');
    if (BStringArray__len(&segments) == 0)
    {
        return path;
    }

    match_optional(BString, BStringArray__at(&segments, BStringArray__len(&segments) - 1),
        some(name)
        {
            return name;
        },
        none() { return path; }
    )
}

Result(CollectionCode) IOSubsystem__append_dev_filesystem_nodes(
    IOSubsystemPtr self, FilesystemPtr filesystem, FilesystemNodes * target
)
{
    FAIterate(IODevicesArray, self->devices, index, registration)
        if (IOSubsystem__is_dev_path(&registration.path))
        {
            match_result(CollectionCode, FilesystemNodes__push(
                target,
                FilesystemNode__heap_new(filesystem, IOSubsystem__dev_name_from_path(registration.path), 0)
            ),
                success(_) {},
                error(code, message) { return Error(CollectionCode, message, code); }
            )
        }

    End
    return Success(CollectionCode, 0);
}

Result(IOHandle) IOSubsystem__record_open_handle(
    IOSubsystemPtr self, IOContextPtr context, BString path, IOInternalHandle handle
)
{
    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &path),
        some(match)
        {
            DeviceRegistrationPtr registration = match.value;
            IOHandle external_handle = (IOHandle) handle | (((IOHandle)_to_device_handle(match.index)) << 32);
            IOHandlesMap__set(&self->handles, external_handle, registration);
            match_optional(ProcessID, context->kernel_context.pid,
                some(pid)
                {
                    match_optional(ProcessHandleMap__value_type_ptr, ProcessHandleMap__get_ptr(&self->process_handles, pid),
                        some(handles)
                        {
                            PIDHandles__push(handles, external_handle);
                        },
                        none()
                        {
                            PIDHandles pid_handles = PIDHandles__new();
                            PIDHandles__push(&pid_handles, external_handle);
                            ProcessHandleMap__set(&self->process_handles, pid, pid_handles);
                        }
                    )
                },
                none()
                {
                    __d_println(PANIC, "TODO: No process found for open, must be kernel, should track its handle");
                }
            );
            return Success(IOHandle, external_handle);
        },
        none() {}
    );

    return Error(IOHandle, BString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, IOContextPtr context, BString path, Flags flags)
{
    __d_printf(IOSUB, "Opening %p\n", self);
    Result(IOHandle) error = Error(IOHandle, BString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);

    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &path),
        some(match)
        {
            __d_printf(IOSUB, "Opening device %p %p %i\n", match.value->device, match.value->device->open, match.index);
            DeviceRegistrationPtr registration = match.value;
            Result(IOInternalHandle) open_result = registration->device->open(registration->state, path, flags);
            __d_printf(IOSUB, "Opened device %p %i\n", match.value->device, match.index);

            match_result(IOInternalHandle, open_result,
                success(handle) { return IOSubsystem__record_open_handle(self, context, path, handle); },
                error(code, message) { return Error(IOHandle, message, code); }
            )
        },
        none() {}
    );

    return error;
}

static DirectoryEntry DirectoryEntry__from_node(FilesystemNodePtr node)
{
    return DirectoryEntry__new(
        node->attribs,
        node->user_id,
        node->group_id,
        node->mode,
        &node->name
    );
}

Result(DirectoryEntryCount) IOSubsystem__list_dir(
    IOSubsystemPtr self, BString path, DirectoryEntryPtr entries, size_t max_entries
)
{
    BString root_path = BSTRING_NEW("/");
    if (BString__equals(&path, &root_path))
    {
        __d_printf(IOSUB, "IOSubsystem__list_dir root path=%s max=%i\n", cstr(path), (int) max_entries);
    }
    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &path),
        some(match)
        {
            DeviceRegistrationPtr registration = match.value;
            if (BString__equals(&path, &root_path))
            {
                __d_printf(IOSUB, "IOSubsystem__list_dir matched registration path=%s state=%p device=%p\n",
                    cstr(registration->path), registration->state, registration->device);
            }
            if (!BString__equals(&registration->path, &root_path))
            {
                return Error(DirectoryEntryCount, bstring("Directory listing not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            if (file_io->list_dir != NULL) // hundelint: ignore-no-null
            {
                return file_io->list_dir(registration->state, path, entries, max_entries);
            }

            match_result(FilesystemNodes, file_io->list_nodes(registration->state, NULL, path), // hundelint: ignore-no-null
                success(nodes)
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
                },
                error(code, message) { return Error(DirectoryEntryCount, message, code); }
            )
        },
        none() {}
    );

    return Error(DirectoryEntryCount, BString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__mkdir(IOSubsystemPtr self, BString path)
{
    BString root_path = BSTRING_NEW("/");
    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &path),
        some(match)
        {
            DeviceRegistrationPtr registration = match.value;
            if (!BString__equals(&registration->path, &root_path))
            {
                return Error(IOStatus, bstring("mkdir not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            return file_io->mkdir(registration->state, path);
        },
        none() {}
    );

    return Error(IOStatus, BString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__remove(IOSubsystemPtr self, BString path, Flags flags)
{
    BString root_path = BSTRING_NEW("/");
    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &path),
        some(match)
        {
            DeviceRegistrationPtr registration = match.value;
            if (!BString__equals(&registration->path, &root_path))
            {
                return Error(IOStatus, bstring("remove not supported"), IO_OPERATION_NOT_SUPPORTED);
            }

            FileIOPtr file_io = (FileIOPtr) registration->device;
            return file_io->remove(registration->state, path, flags);
        },
        none() {}
    );

    return Error(IOStatus, BString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__link(IOSubsystemPtr self, BString source_path, BString target_path)
{
    BString root_path = BSTRING_NEW("/");
    match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &source_path),
        some(source_match)
        {
            match_optional(IODevicesArray__FindResult, IOSubsystem__find_device(self, &target_path),
                some(target_match)
                {
                    DeviceRegistrationPtr source_registration = source_match.value;
                    DeviceRegistrationPtr target_registration = target_match.value;
                    if (!BString__equals(&source_registration->path, &root_path)
                        || !BString__equals(&target_registration->path, &root_path))
                    {
                        return Error(IOStatus, bstring("link not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    FileIOPtr source_file_io = (FileIOPtr) source_registration->device;
                    FileIOPtr target_file_io = (FileIOPtr) target_registration->device;
                    if (source_file_io != target_file_io || source_registration->state != target_registration->state)
                    {
                        return Error(IOStatus, bstring("cross-filesystem link not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    return source_file_io->link(source_registration->state, source_path, target_path);
                },
                none() {}
            );
        },
        none() {}
    );

    return Error(IOStatus, BString__sprintf(&no_device_found_format, cstr(source_path)), IO_NO_DEVICE_FOUND);
}

Result(IOStatus) IOSubsystem__mount(
    IOSubsystemPtr self, BString device_path, BString filesystem_type, BString mountpoint
)
{
    match_optional(FilesystemPtr, self->root_filesystem,
        some(root_fs) { return Filesystem__mount_block_device(root_fs, device_path, filesystem_type, mountpoint); },
        none() {}
    )

    return Error(IOStatus, bstring("No root filesystem"), IO_NO_DEVICE_FOUND);
}

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContextPtr context)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            __d_printf(IOSUB, "IOSubsystem__close start handle=%i context=%p\n", handle, context);
            critical_section(self->lock,
            {
                __d_printf(IOSUB, "IOSubsystem__close locked handle=%i\n", handle);
                match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                    some(registration)
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
                        match_optional(ProcessID, context->kernel_context.pid,
                            some(pid)
                            {
                                __d_printf(IOSUB, "IOSubsystem__close removing pid handle pid=%i handle=%i\n", pid, handle);
                                match_optional(ProcessHandleMap__value_type_ptr, ProcessHandleMap__get_ptr(&self->process_handles, pid),
                                    some(proc_handles)
                                    {
                                        PIDHandles__remove(proc_handles, handle);
                                    },
                                    none() {}
                                )
                            },
                            none()
                            {
                                __d_println(PANIC, "TODO: No process found for close, must be kernel, should track its handle");
                            }
                        );

                        match_result(IOInternalHandle, close_res,
                            success(_)
                            {
                                __d_printf(IOSUB, "IOSubsystem__close success handle=%i\n", handle);
                                cs_return Success(IOHandle, handle);
                            },
                            error(code, message)
                            {
                                __d_printf(IOSUB, "IOSubsystem__close device error handle=%i code=%i\n", handle, code);
                                cs_return Error(IOHandle, message, code);
                            }
                        );
                    },
                    none() {}
                );
            });

            __d_printf(IOSUB, "IOSubsystem__close unknown handle=%i\n", handle);
            BString error_msg = BString__sprintf(&unknown_handle_format, handle) ;
            return Error(IOHandle, error_msg, IO_NOT_OPEN);
        },
        none()
        {
            return Error(IOHandle, no_handle_supplied, IO_NOT_OPEN);
        }
    )
    return Error(IOHandle, unreachable_code, IO_UNKNOWN_HANDLE);
}

Result(ByteCount) IOSubsystem__transfer(IOBufferPtr buffer, DataPtr result, size_t size)
{
    __d_printf(DEBUG, "reading %i %i\n", buffer->read_pos, buffer->write_pos);
    match_result(ItemCount, IOBuffer__drain(buffer, result, size),
        success(num)
        {
            return Success(ByteCount, num);
        },
        error(code, message) { return Error(ByteCount, message, code); }
    )
}

Result(IOStatus) IOSubsystem__data_arrived(IOSubsystemPtr self, IOContextPtr parked_context)
{
    __d_println(SCHEDULER, "Data arrived");
    __d_printf(IOSUB, "IO data arrived parked_context=%p\n", parked_context);
    match_optional(IOHandle, parked_context->handle,
        some(handle)
        {
            __d_printf(IOSUB, "IO data arrived handle=0x%x\n", handle);
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(match)
                {
                    __d_printf(IOSUB, "IO data arrived device=%p state=%p\n", match->device, match->state);
                    match_result(ParkStatus, match->device->unpark(match->state),
                        success(_) {},
                        error(code, message) { return Error(IOStatus, message, code); }
                    )
                    __d_println(IOSUB, "IO data arrived device unpark done");

                    match_optional(ProcessID, parked_context->kernel_context.pid,
                        some(pid)
                        {
                            __d_println(IOSUB, "IO sub unparking process");
                            __d_printf(IOSUB, "IO data arrived parked pid=%i handle=%i\n", pid, handle);
                            MessagePtr message = Message__heap_new_io_operation(
                                SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, handle
                            );

                            match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
                                success(ack)
                                {
                                    __d_printf(IOSUB, "IO data arrived message delivered pid=%i ack=%i\n", pid, ack);
                                    __d_println(IOSUB, "IO sub unparked");
                                    return Success(IOStatus, IO_OK);
                                },
                                error(code, message) { return Error(IOStatus, message, code); }
                            )
                        },
                        none() { return Error(IOStatus, no_pid_found, IO_UNKNOWN_PID); }
                    );
                },
                none() { return Error(IOStatus, bstring("No device found for parking context"), IO_NOT_OPEN);; }
            )
        },
        none() { return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN); }
    )

    return Error(IOStatus, unreachable_code, IO_PANIC);
}

static Result(ParkStatus) IOSubsystem__park(IOSubsystemPtr self, IOContextPtr io_context)
{
    __d_println(IOSUB, "Parking io");
    match_optional(IOHandle, io_context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(match)
                {
                    __d_println(IOSUB, "Parking context to device");
                    match_result(ParkStatus, match->device->park(match->state, io_context),
                        success(_) {},
                        error(code, message) { return Error(ParkStatus, message, code); }
                    )

                    DeviceHandle handle_device = device_handle(handle);
                    match_optional(ProcessPtr, IOServerProcessMap__get(&self->read_server_processes, handle_device),
                        some(server_process)
                        {
                            ServiceEvent event;
                            event.type = SERVICE_EVENT_IO;
                            event.source = SERVICE_IO_READ_REQUEST;
                            event.data = handle_device;
                            match_result(ServiceCode, IOSubsystem__deliver_service_event(self, server_process, event),
                                success(_) {},
                                error(code, message) { __d_printf(PANIC, "Failed to deliver IO read service event: %s", cstr(message)); }
                            )
                        },
                        none() {}
                    )

                    return Success(ParkStatus, IO_PARK_OK);
                },
                none() { return Error(ParkStatus, bstring("No device found for parking context"), IO_NOT_OPEN);; }
            )
        },
        none() { return Error(ParkStatus, no_handle_supplied, IO_NOT_OPEN); }
    )

    return Error(ParkStatus, unreachable_code, IO_NOT_OPEN);
}

Result(Anything) IOSubsystem__continue_read(ParkedOperationPtr parked_operation)
{
    KernelContextPtr kernel_context = parked_operation->kernel_context;
    IOContextPtr io_context = (IOContextPtr) kernel_context;

    __d_printf(IOSUB, "\nContinuing read %p %p\n", io_context, io_context->io_subsystem);
    match_optional(IOSubsystemPtr, io_context->io_subsystem,
        some(io_subsystem)
        {
            match_result(ByteCount, IOSubsystem__read(io_subsystem, io_context),
                success(bytes)
                {
                    return Success(Anything, bytes);
                },
                error(code, message) { return Error(Anything, message, code); }
            )
        },
        none() { return Error(Anything, bstring("no io subsystem found"), IO_PANIC); }
    )

    return Error(Anything, bstring("unexpected code path"), IO_PANIC);
}

static Result(ByteCount) IOSubsystem__request_read_park(IOSubsystemPtr self, IOContextPtr context)
{
    __d_println(IOSUB, "Parking waiting for IO");

    IOContextPtr parked_context = IOContext__from_io_context(context, context->handle);

    match_result(ParkStatus, IOSubsystem__park(self, parked_context),
        success(_) {},
        error(code, message)
        {
            IOContext__dispose(parked_context);
            return Error(ByteCount, message, code);
        }
    )

    match_optional(ProcessID, parked_context->kernel_context.pid,
        some(pid)
        {
            match_optional(IOHandle, parked_context->handle,
                some(handle)
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

                    match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
                        success(_) { return Error(ByteCount, bstring("IO read parked"), IO_WAIT_FOR_DATA); },
                        error(code, message) { return Error(ByteCount, message, code); }
                    )
                },
                none() { return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN); }
            )
        },
        none() { return Error(ByteCount, no_pid_found, IO_UNKNOWN_PID); }
    )

    return Error(ByteCount, unreachable_code, IO_PANIC);
}

Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContextPtr context)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            Optional(DeviceRegistrationPtr) device_opt = None(DeviceRegistrationPtr);

            critical_section(self->lock,
            {
                device_opt = IOHandlesMap__get(&self->handles, handle);
            })
            match_optional(DeviceRegistrationPtr, device_opt,
                some(registration)
                {
                    match_optional(ExternalBuffer, context->buffer,
                        some(ext_buffer)
                        {
                            Result(IOBufferPtr) read_res = registration->device->read(
                                registration->state, device_internal_handle(handle), ext_buffer.len, ext_buffer
                            );
                            match_result(IOBufferPtr, read_res,
                                success(buffer)
                                {
                                    __d_printf(IOSUB, "reading %p %p\n", context->buffer, buffer);
                                    match_result(ByteCount, IOSubsystem__transfer(buffer, ext_buffer.data, ext_buffer.len),
                                        success(bytes)
                                        {
                                            if (bytes > 0
                                                && registration->state == (IOStatePtr) StreamState__instance()
                                                && BString__equals(&registration->path, &tty_path))
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
                                        },
                                        error(code, message) { return Error(ByteCount, message, code); }
                                    )
                                },
                                error(code, error_message)
                                {
                                    if (code == IO_WAIT_FOR_DATA && registration->device->park != NULL) // hundelint: ignore-no-null
                                    {
                                        return IOSubsystem__request_read_park(self, context);
                                    }
                                    return Error(ByteCount, error_message, code);
                                }
                            )
                        },
                        none() {}
                    );
                },
                none() { __d_println(PANIC, "No device found for handle"); }
            );
        },
        none()
        {
            return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN);
        }
    )
    return Error(ByteCount, unreachable_code, IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContextPtr context)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(registration)
                {
                    match_optional(ExternalBuffer, context->buffer,
                        some(buffer)
                        {
                            Result(ByteCount) write_result = registration->device->write(
                                registration->state, device_internal_handle(handle), buffer.data, buffer.len
                            );
                            match_result(ByteCount, write_result,
                                success(count)
                                {
                                    match_optional(ProcessPtr, IOServerProcessMap__get(&self->write_server_processes, device_handle(handle)),
                                        some(server_process)
                                        {
                                            ServiceEvent event;
                                            event.type = SERVICE_EVENT_IO;
                                            event.source = SERVICE_IO_WRITE_REQUEST;
                                            event.data = device_handle(handle);
                                            match_result(ServiceCode, IOSubsystem__deliver_service_event(self, server_process, event),
                                                success(_) {},
                                                error(code, message) { __d_printf(PANIC, "Failed to deliver IO write service event: %s", cstr(message)); }
                                            )
                                        },
                                        none() {}
                                    )
                                    return Success(ByteCount, count);
                                },
                                error(code, message) { return Error(ByteCount, message, code); }
                            )
                        },
                        none()
                        {
                            return Error(ByteCount, no_buffer_supplied, IO_UNKNOWN_BUFFER);
                        }
                    );
                },
                none() {}
            );
        },
        none() {}
    )

    return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOHandle) IOSubsystem__server_register(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, BString path, IOServerRole role
)
{
    match_result(IOHandle, IOSubsystem__open(self, context, path, IO_OPEN_WRITE),
        success(handle)
        {
            if (role == IO_SERVER_WRITE)
            {
                IOServerProcessMap__set(&self->write_server_processes, device_handle(handle), process);
            }
            else
            {
                IOServerProcessMap__set(&self->read_server_processes, device_handle(handle), process);
                ServiceEvent event;
                event.type = SERVICE_EVENT_IO;
                event.source = SERVICE_IO_READ_REQUEST;
                event.data = device_handle(handle);
                match_result(ServiceCode, IOSubsystem__deliver_service_event(self, process, event),
                    success(_) {},
                    error(code, message) { return Error(IOHandle, message, code); }
                )
            }
            IOServerRoleMap__set(&self->server_roles, handle, role);
            return Success(IOHandle, handle);
        },
        error(code, message) { return Error(IOHandle, message, code); }
    )

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
        .kernel_addr = Kernel__from_virtual_address(Kernel__instance(), kernel_page),
        .read = true,
        .write = true,
        .execute = false,
        .valid = true,
    };

    PageTable__map(v_ptr(process->page_table, PageTable), Kernel__instance(), user_address, map_size);
    match_result(ProcessMemoryCode, Process__record_user_allocation(process, user_address, map_size, USER_ALLOCATION_DYNAMIC),
        success(_)
        {
            process->allocation_next += map_size;
            return Success(IOServerBuffer, ((IOServerBuffer) {
                .data = (IOServerBufferDataPtr) (user_page + offset),
                .capacity = buffer.capacity,
            }));
        },
        error(code, message) { return Error(IOServerBuffer, message, code); }
    )

    return Error(IOServerBuffer, no_buffer_supplied, IO_UNKNOWN_BUFFER);
}

Result(IOStatus) IOSubsystem__server_get_buffer(
    IOSubsystemPtr self, IOContextPtr context, ProcessPtr process, IOServerBufferPtr target
)
{
    if (target == NULL) // hundelint: ignore-no-null
    {
        return Error(IOStatus, no_buffer_supplied, IO_UNKNOWN_BUFFER);
    }

    match_optional(IOHandle, context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(registration)
                {
                    IOServerRole role = IO_SERVER_READ;
                    match_optional(IOServerRole, IOServerRoleMap__get(&self->server_roles, handle),
                        some(registered_role)
                        {
                            role = registered_role;
                        },
                        none() {}
                    )
                    IO_server_buffer buffer_getter = registration->device->server_buffer;
                    if (role == IO_SERVER_WRITE)
                    {
                        buffer_getter = registration->device->server_write_buffer;
                    }

                    if (buffer_getter == NULL) // hundelint: ignore-no-null
                    {
                        return Error(IOStatus, bstring("server buffer not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    match_result(IOServerBuffer, buffer_getter(registration->state),
                        success(kernel_buffer)
                        {
                            match_result(IOServerBuffer, IOSubsystem__map_server_buffer(process, kernel_buffer),
                                success(user_buffer)
                                {
                                    *target = user_buffer;
                                    return Success(IOStatus, IO_OK);
                                },
                                error(code, message) { return Error(IOStatus, message, code); }
                            )
                        },
                        error(code, message) { return Error(IOStatus, message, code); }
                    )
                },
                none() {}
            );
        },
        none() {}
    )

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_data_available(IOSubsystemPtr self, IOContextPtr context, size_t byte_count)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(registration)
                {
                    if (registration->device->data_available == NULL) // hundelint: ignore-no-null
                    {
                        return Error(IOStatus, bstring("data available not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    match_optional(IOContextPtr, registration->device->data_available(registration->state, byte_count),
                        some(parked_context)
                        {
                            match_result(IOStatus, IOSubsystem__data_arrived(self, parked_context),
                                success(status) { return Success(IOStatus, status); },
                                error(code, message) { return Error(IOStatus, message, code); }
                            )
                        },
                        none() {}
                    )

                    return Success(IOStatus, IO_OK);
                },
                none() {}
            );
        },
        none() {}
    )

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_data_consumed(IOSubsystemPtr self, IOContextPtr context, size_t byte_count)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(registration)
                {
                    if (registration->device->data_consumed == NULL) // hundelint: ignore-no-null
                    {
                        return Error(IOStatus, bstring("data consumed not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }
                    return registration->device->data_consumed(registration->state, byte_count);
                },
                none() {}
            );
        },
        none() {}
    )

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(IOStatus) IOSubsystem__server_eof(IOSubsystemPtr self, IOContextPtr context)
{
    match_optional(IOHandle, context->handle,
        some(handle)
        {
            match_optional(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle),
                some(registration)
                {
                    if (registration->device->eof == NULL) // hundelint: ignore-no-null
                    {
                        return Error(IOStatus, bstring("eof not supported"), IO_OPERATION_NOT_SUPPORTED);
                    }

                    match_optional(IOContextPtr, registration->device->eof(registration->state),
                        some(parked_context)
                        {
                            match_result(IOStatus, IOSubsystem__data_arrived(self, parked_context),
                                success(status) { return Success(IOStatus, status); },
                                error(code, message) { return Error(IOStatus, message, code); }
                            )
                        },
                        none() {}
                    )

                    return Success(IOStatus, IO_OK);
                },
                none() {}
            );
        },
        none() {}
    )

    return Error(IOStatus, no_handle_supplied, IO_NOT_OPEN);
}

Result(Anything) IOSubsystem__dispose_of_process_resources(IOSubsystemPtr self, IOContextPtr context)
{
    match_optional(ProcessID, context->kernel_context.pid,
        some(pid)
        {
            // map get a copy of the list as the handles will be removed as we call close
            match_optional(ProcessHandleMap__value_type, ProcessHandleMap__get(&self->process_handles, pid),
                some(handles)
                {
                    FAIterate(PIDHandles, handles, index, handle)
                        IOContextPtr handle_context = IOContext__from_io_context(context, Some(IOHandle, handle));
                        IOSubsystem__close(self, handle_context);
                    End
                },
                none() {}
            )
            ProcessHandleMap__delete(&self->process_handles, pid);
            return Success(Anything, pid);
        },
        none() { __d_println(PANIC, "No PID found for close all"); }
    )

    return Error(Anything, no_pid_found, IO_UNKNOWN_PID);

}

void IOSubsystem__message_received(KernelListenerPtr listener, MessagePtr message)
{
    if (message->source_code != PROC_STATE_DEAD)
    {
        return;
    }
    match_optional(Payload, message->payload,
        some(payload)
        {
            if (payload.type == PAYLOAD_TYPE_PROCESS_ID)
            {
                ProcessID pid = payload.process_id;
                IOContextPtr context = IOContext__from_message_listener(listener, pid);
                IOSubsystem__dispose_of_process_resources(listener->kernel->io_subsystem, context);
                IOContext__dispose(context);
            }
        },
        none() {}
    )
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

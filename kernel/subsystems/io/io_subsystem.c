#include "io_subsystem.h"

#include "process.h"
#include "../../types/result.h"
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../debug.h"
#include "scheduler/scheduler.h"

static KString no_device_found_format = KSTRING_NEW("No device found %s\n");
static KString failed_to_register_format = KSTRING_NEW("Failed to register device %s\n");
static KString unknown_handle_format = KSTRING_NEW("Unknown handle %i\n");
static KString no_handle_supplied = KSTRING_NEW("No handle supplied");
static KString unreachable_code = KSTRING_NEW("Unreachable code path");
static KString no_buffer_supplied = KSTRING_NEW("No buffer supplied");

DeviceHandle _to_device_handle(ArrayIndex index)
{
    return (index + 1) & 0xFFFFFFFF;
}

IOInternalHandle _device_internal_handle(IOHandle handle)
{
    return handle & 0xFFFFFFFF;
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

    Result(ArrayCode) index_res = IODevicesArray__push(&self->devices, reg);

    ResultMatch(ArrayCode, index_res);
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
    return KString__starts_with(path, &device->path);
}

static FindResultOptional(IODevicesArray) IOSubsystem__find_device(IOSubsystemPtr self, KStringPtr path)
{
    return IODevicesArray__find(&self->devices, path_match, path);
}

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, IOContext context, KString path, Flags flags)
{
    Result(IOHandle) error = Error(IOHandle, KString__sprintf(&no_device_found_format, cstr(path)), IO_NO_DEVICE_FOUND);

    FindResultMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(match,
        {
            DeviceRegistrationPtr registration = match.value;
            Result(IOInternalHandle) open_result = registration->device->open(registration->state, path, flags);

            ResultMatch(IOInternalHandle, open_result);
                IsSuccess(handle,
                {
                    IOHandle external_handle = (IOHandle) handle | (((IOHandle)_to_device_handle(match.index)) << 32);
                    IOHandlesMap__set(&self->handles, external_handle, registration);
                    Match(ProcessPtr, context.kernel_context.proc)
                        IsSome(process,
                        {
                            ProcessHandles__push(&process->handles, external_handle);
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

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOContext context)
{
    Match(IOHandle, context.handle)
        IsSome(handle,
        {
            __d_printf(IOSUB, "Closing handle %i\n", handle);
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    Result(IOInternalHandle) close_res = registration->device->close(registration->state, _device_internal_handle(handle));
                    IOHandlesMap__delete(&self->handles, handle);
                    Match(ProcessPtr, context.kernel_context.proc)
                        IsSome(process,
                        {
                            ProcessHandles__remove(&process->handles, handle);
                        });
                        IsNone(
                        {
                            __d_println(PANIC, "TODO: No process found for close, must be kernel, should track its handle");
                        });
                    End


                    ResultMatch(IOInternalHandle, close_res)
                        IsSuccess(_, return Success(IOHandle, handle));
                        IsError(code, message, return Error(IOHandle, message, code));
                    End
                });
            End

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

void IOSubsystem__data_arrived(IOSubsystemPtr self, IOContext parked_context)
{
    __d_println(IOSUB, "Data arrived");
    Match(IOHandle, parked_context.handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(match,
                {
                    match->device->unpark(match->state);
                    // tell scheduler things are ready to complete the parked call
                    __d_println(IOSUB, "IO sub unparking process");
                    Match(ProcessPtr, parked_context.kernel_context.proc)
                        IsSome(process, Scheduler__unpark(self->scheduler, process));
                        IsNone(__d_println(PANIC, "No process found for parked context"));
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

void IOSubsystem__park(IOSubsystemPtr self, IOContext io_context)
{
    __d_println(IOSUB, "Parking io");
    Match(IOHandle, io_context.handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(match,
                {
                    __d_println(IOSUB, "Parking context to device");
                    match->device->park(match->state, io_context);
                })
                IsNone(
                    __d_println(IOSUB, "No device found for parking context");
                )
            End
        })
        IsNone(__d_println(PANIC, "No handle supplied"))
    End
}

Result(Anything) IOSubsystem__continue_read(void * context)
{
    IOContextPtr io_context = (IOContextPtr) context;
    __d_printf(IOSUB, "\nContinuing read %p %p\n", io_context, io_context->io_subsystem);
    ResultMatch(ByteCount, IOSubsystem__read(io_context->io_subsystem, *io_context))
        IsSuccess(bytes, return Success(Anything, bytes));
        IsError(code, message, return Error(Anything, message, code));
    End
    return Error(Anything, kstring("unexpected code path"), 0);
}

Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContext context)
{
    __d_printf(IOSUB, "Read handle %p\n", context.handle);
    Match(IOHandle, context.handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    Match(ExternalBuffer, context.buffer)
                        IsSome(ext_buffer,
                        {
                            Result(IOBufferPtr) read_res = registration->device->read(
                                registration->state, _device_internal_handle(handle), ext_buffer.len
                            );
                            ResultMatch(IOBufferPtr, read_res)
                                IsSuccess(buffer,
                                {
                                    __d_printf(IOSUB, "reading %p %p\n", context.buffer, buffer);
                                    return IOSubsystem__transfer(buffer, ext_buffer.data, ext_buffer.len);
                                })
                                IsError(code, message,
                                {
                                    if (code == IO_WAIT_FOR_DATA)
                                    {
                                        __d_println(IOSUB, "Parking waiting for IO");
                                        Optional(Continuation) continuation = Some(Continuation, IOSubsystem__continue_read);

                                        IOSubsystem__park(self, context);
                                        Match(ProcessPtr, context.kernel_context.proc)
                                            IsSome(process,
                                            {
                                                Scheduler__park(self->scheduler, process, &context.kernel_context, continuation);
                                            })
                                            IsNone(__d_println(PANIC, "No process found for parked context"));
                                        End

                                    }
                                    return Error(ByteCount, message, code);
                                })
                            End
                        });
                    End
                })
            End
        })
        IsNone(
        {
            return Error(ByteCount, no_handle_supplied, IO_NOT_OPEN);
        })
    End
    return Error(ByteCount, unreachable_code, IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContext context)
{
    __d_printf(DEBUG, "Writing handle %i\n", context.handle);
    Match(IOHandle, context.handle)
        IsSome(handle,
        {
            Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
                IsSome(registration,
                {
                    Match(ExternalBuffer, context.buffer)
                        IsSome(buffer,
                        {
                            return registration->device->write(
                                registration->state, _device_internal_handle(handle), buffer.data, buffer.len
                            );
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

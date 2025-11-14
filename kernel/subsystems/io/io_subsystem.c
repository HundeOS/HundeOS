#include "io_subsystem.h"

#include "process.h"
#include "../../types/result.h"
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../debug.h"
#include "scheduler/scheduler.h"


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

    KString format = kstring("Failed to register device %s\n");
    return Error(DeviceHandle, KString__sprintf(&format, path), IO_REGISTER_FAILED);
}

bool path_match(DeviceRegistration * device, void * v_path)
{
    KString * path = v_path;
    return KString__equals(path, &device->path);
}

const char * NO_DEVICE_FOUND = "No device found %s\n";

static FindResultOptional(IODevicesArray) IOSubsystem__find_device(IOSubsystemPtr self, KStringPtr path)
{
    return IODevicesArray__find(&self->devices, path_match, path);
}

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, ProcessPtr proc, KString path, Flags flags)
{
    KString format = kstring(NO_DEVICE_FOUND);
    KString error_msg = KString__sprintf(&format, &path);

    Result(IOHandle) error = Error(IOHandle, error_msg, IO_NO_DEVICE_FOUND);

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
                    ProcessHandles__push(&proc->handles, external_handle);
                    return Success(IOHandle, external_handle);
                });
                IsError(code, message, return Error(IOHandle, message, code))
            End
        });
    End

    return error;
}

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, ProcessPtr proc, IOHandle handle)
{
    __d_printf(IOSUB, "Closing handle %i\n", handle);
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            Result(IOInternalHandle) close_res = registration->device->close(registration->state, _device_internal_handle(handle));
            IOHandlesMap__delete(&self->handles, handle);
            ProcessHandles__remove(&proc->handles, handle);

            ResultMatch(IOInternalHandle, close_res)
                IsSuccess(_, return Success(IOHandle, handle));
                IsError(code, message, return Error(IOHandle, message, code));
            End
        });
    End
    KString format = kstring("Unknown handle %p\n");
    KString error_msg = KString__sprintf(&format, handle) ;
    return Error(IOHandle, error_msg, IO_NOT_OPEN);
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
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, parked_context.handle));
        IsSome(match,
        {
            match->device->unpark(match->state);
            // tell scheduler things are ready to complete the parked call
            __d_println(IOSUB, "IO sub unparking process");
            Scheduler__unpark(self->scheduler, parked_context.proc);
        })
        IsNone(
            __d_println(PANIC, "No device found for parking context");
        )
    End

}

void IOSubsystem__park(IOSubsystemPtr self, IOContext io_context)
{
    __d_println(IOSUB, "Parking io");
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, io_context.handle));
        IsSome(match,
        {
            __d_println(IOSUB, "Parking context to device");
            match->device->park(match->state, io_context);
        })
        IsNone(
            __d_println(IOSUB, "No device found for parking context");
        )
    End
}

Result(Anything) IOSubsystem__continue_read(void * context)
{
    IOContextPtr io_context = (IOContextPtr) context;
    __d_printf(IOSUB, "\nConinuining read %p %p\n", io_context, io_context->io_subsystem);
    ResultMatch(ByteCount, IOSubsystem__read(io_context->io_subsystem, *io_context))
        IsSuccess(bytes, return Success(Anything, bytes));
        IsError(code, message, return Error(Anything, message, code));
    End
    return Error(Anything, kstring("unexpected code path"), 0);
}

Result(ByteCount) IOSubsystem__read(IOSubsystemPtr self, IOContext context)
{
    __d_printf(IOSUB, "Read handle %p\n", context.handle);
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, context.handle));
        IsSome(registration,
        {
            Result(IOBufferPtr) read_res = registration->device->read(
                registration->state, _device_internal_handle(context.handle), context.size
            );
            ResultMatch(IOBufferPtr, read_res)
                IsSuccess(buffer,
                {
                    __d_printf(IOSUB, "reading %p %p\n", context.buffer, buffer);
                    return IOSubsystem__transfer(buffer, context.buffer, context.size);
                })
                IsError(code, message,
                {
                    if (code == IO_WAIT_FOR_DATA)
                    {
                        __d_println(IOSUB, "Parking waiting for IO");
                        Optional(Continuation) continuation = Some(Continuation, IOSubsystem__continue_read);

                        IOSubsystem__park(self, context);
                        Scheduler__park(self->scheduler, context.proc, &context.kernel_context, continuation);
                    }
                    return Error(ByteCount, message, code);
                })
            End
        });
    End

    KString format = kstring("Unknown handle %p\n");
    KString error_msg = KString__sprintf(&format, context.handle) ;
    return Error(ByteCount, error_msg, IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOContext context)
{
    __d_printf(DEBUG, "Writing handle %i\n", context.handle);
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, context.handle));
        IsSome(registration,
        {
            return registration->device->write(
                registration->state, _device_internal_handle(context.handle), context.buffer, context.size);
        });
    End

    KString format = kstring("Unknown handle %p\n");
    KString error_msg = KString__sprintf(&format, context.handle);
    return Error(ByteCount, error_msg, IO_NOT_OPEN);
}

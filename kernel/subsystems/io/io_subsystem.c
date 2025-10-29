#include "io_subsystem.h"

#include "process.h"
#include "../../types/result.h"
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../debug.h"

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
    __d_printf(DEBUG, "Closing handle %i\n", handle);
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

Result(ByteCount) IOSubsystem__read(
        IOSubsystemPtr self, ProcessPtr proc, IOHandle handle, DataPtr result_buffer, size_t size
    )
{
    __d_printf(DEBUG, "Read handle %p\n", handle);
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            Result(IOBufferPtr) read_res = registration->device->read(registration->state, _device_internal_handle(handle), size);
            ResultMatch(IOBufferPtr, read_res)
                IsSuccess(buffer,
                {
                    __d_printf(DEBUG, "reading %p %p\n", result_buffer, buffer);
                    return IOSubsystem__transfer(buffer, result_buffer, size);
                })
                IsError(code, message,
                {
                    return Error(ByteCount, message, code);
                })
            End
        });
    End

    KString format = kstring("Unknown handle %p\n");
    KString error_msg = KString__sprintf(&format, handle) ;
    return Error(ByteCount, error_msg, IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, ProcessPtr proc, IOHandle handle, DataPtr data, size_t size)
{
    __d_printf(DEBUG, "Writing handle %i\n", handle);
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            return registration->device->write(registration->state, _device_internal_handle(handle), data, size);
        });
    End
    KString format = kstring("Unknown handle %p\n");
    KString error_msg = KString__sprintf(&format, handle) ;
    return Error(ByteCount, error_msg, IO_NOT_OPEN);
}

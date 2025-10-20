#include "io_subsystem.h"
#include "../../types/result.h"
#include "../../types/fixed_array.h"
#include "../../types/optional.h"

Result(DeviceHandle) IOSubsystem__register_device(IOSubsystemPtr self, KString path, IOPtr device, IOStatePtr device_state)
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
        IsSuccess(handle,
        {
            return Success(DeviceHandle, handle);
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

static FAPtrOptional(IODevicesArray) IOSubsystem__find_device(IOSubsystemPtr self, KStringPtr path)
{
    return IODevicesArray__find(&self->devices, path_match, path);
}

Result(IOHandle) IOSubsystem__open(IOSubsystemPtr self, KString path, Flags flags)
{
    KString format = kstring(NO_DEVICE_FOUND);
    KString error_msg = KString__sprintf(&format, &path);

    Result(IOHandle) error = Error(IOHandle, error_msg, IO_NO_DEVICE_FOUND);

    FAPtrMatch(IODevicesArray, IOSubsystem__find_device(self, &path));
        IsSome(device,
        {
            Result(IOHandle) open_result = device->device->open(device->state, path, flags);

            ResultMatch(IOHandle, open_result);
                IsSuccess(handle,
                {
                    IOHandlesMap__set(&self->handles, handle, device);
                    return Success(IOHandle, handle);
                });
            End
            return open_result;
        });
        IsNone(
        {
            return error;
        });
    End

    return error;
}

Result(IOHandle) IOSubsystem__close(IOSubsystemPtr self, IOHandle handle)
{
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            Result(IOHandle) close_res = registration->device->close(registration->state, handle);
            IOHandlesMap__delete(&self->handles, handle);
            return close_res;
        });
    End
    return Error(IOHandle, kstring("Unknown handle"), IO_NOT_OPEN);
}

Result(BufferPtr) IOSubsystem__read(IOSubsystemPtr self, IOHandle handle, size_t size)
{
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            Result(BufferPtr) read_res = registration->device->read(registration->state, handle, size);
            return read_res;
        });
    End
    return Error(BufferPtr, kstring("Unknown handle"), IO_NOT_OPEN);
}

Result(ByteCount) IOSubsystem__write(IOSubsystemPtr self, IOHandle handle, BufferPtr buffer, size_t size)
{
    Match(DeviceRegistrationPtr, IOHandlesMap__get(&self->handles, handle));
        IsSome(registration,
        {
            return registration->device->write(registration->state, handle, buffer, size);
        });
    End
    return Error(ByteCount, kstring("Unknown handle"), IO_NOT_OPEN);
}

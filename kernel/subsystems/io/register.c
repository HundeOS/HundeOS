#include "register.h"
#include "io.h"
#include "io_subsystem.h"

#include "console/console.h"

#define IO_REGISTER(io_subsystem, path, io_type, state_type) \
    { \
        IOStatePtr state_ptr = (IOStatePtr) state_type##__heap_new(); \
        IOPtr io_ptr = io_type##__heap_new(); \
        \
        Result(DeviceHandle) register_res = IOSubsystem__register_device(io_subsystem, path, io_ptr, state_ptr); \
        \
        ResultMatch(DeviceHandle, register_res); \
            IsError(code, message, \
            { \
                return Error(RegisterCode, message, code); \
            }) \
        End \
    } \


static const char * CONSOLE = "/dev/console";

Result(RegisterCode) IOSubsystem__register_devices(IOSubsystemPtr self)
{
    KString console_path = kstring(CONSOLE);
    IO_REGISTER(self, console_path, Console, ConsoleState);

    return Success(RegisterCode, IO_OK);
}
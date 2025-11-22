#include "register.h"
#include "io.h"
#include "io_subsystem.h"

#include "console/console.h"
#include "file/filesystem.h"

#define IO_REGISTER(io_subsystem, path, io_ptr, io_state) \
    { \
        \
        Result(DeviceHandle) register_res = IOSubsystem__register_device(io_subsystem, path, (IOPtr) io_ptr, io_state); \
        \
        ResultMatch(DeviceHandle, register_res); \
            IsError(code, message, \
            { \
                return Error(RegisterCode, message, code); \
            }) \
        End \
    } \


static const char * CONSOLE = "::console";
static const char * ROOT = "/";

Result(RegisterCode) IOSubsystem__register_devices(IOSubsystemPtr self)
{
    KString console_path = kstring(CONSOLE);
    KString root_fs_path = kstring(CONSOLE);
    FilesystemPtr root_fs = RootFilesystem__heap_new();
    FilesystemIOStatePtr root_fs_state = FilesystemIOState__heap_new(root_fs);

    IO_REGISTER(self, console_path, Console__heap_new(), (IOStatePtr) ConsoleState__instance());
    IO_REGISTER(self, root_fs_path, root_fs, (IOStatePtr) root_fs_state);

    return Success(RegisterCode, IO_OK);
}
#include "register.h"
#include "file/register.h"

#include "io.h"
#include "io_subsystem.h"
#include "kernel_core.h"

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


static KString CONSOLE = KSTRING_NEW(":console:");
static KString ROOT = KSTRING_NEW("/");

Result(RegisterCode) IOSubsystem__register(KernelPtr kernel)
{
    IOSubsystemPtr io_subsystem = kernel->io_subsystem;
    FilesystemPtr root_fs = Filesystem__build_root_and_mounts();

    IO_REGISTER(io_subsystem, CONSOLE, Console__heap_new(), (IOStatePtr) ConsoleState__instance());
    IO_REGISTER(io_subsystem, ROOT, &root_fs->file_io.base_io, (IOStatePtr) root_fs->state);

    ResultMatch(ListenerRegisterResult, IOSubsystem__register_listeners(kernel))
        IsError(code, message, return Error(RegisterCode, message, IO_REGISTER_FAILED));
    End
    return Success(RegisterCode, IO_OK);
}
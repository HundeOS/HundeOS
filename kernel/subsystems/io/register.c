#include "register.h"
#include "file/register.h"

#include "io.h"
#include "io_subsystem.h"
#include "kernel_core.h"

#include "console/console.h"
#include "file/filesystem.h"
#include "stream/stream.h"

#define IO_REGISTER(io_subsystem, path, io_ptr, io_state) \
    { \
        \
        Result(DeviceHandle) register_res = IOSubsystem__register_device(io_subsystem, path, (IOPtr) io_ptr, io_state); \
        \
        match_result(DeviceHandle, register_res, /* hundelint: ignore-match-branch-macros */ \
            success_do_nothing(), \
            error(code, message) \
            { \
                return Error(RegisterCode, message, code); \
            } \
        ); \
    } \


static BString CONSOLE = bstr(":console:");
static BString TTY = bstr(":tty:");
static BString KEYBOARD = bstr(":keyboard:");
static BString HELLO_STREAM = bstr("/dev/hello");
static BString BUSINFO_STREAM = bstr("/dev/businfo");
static BString ROOT = bstr("/");

Result(RegisterCode) IOSubsystem__register(KernelPtr kernel)
{
    IOSubsystemPtr io_subsystem = kernel->io_subsystem;
    FilesystemPtr root_fs = Filesystem__build_root_and_mounts(kernel);
    io_subsystem->root_filesystem = Some(FilesystemPtr, root_fs);

    IO_REGISTER(io_subsystem, CONSOLE, Console__heap_new(), (IOStatePtr) ConsoleState__instance());
    IO_REGISTER(io_subsystem, TTY, Stream__heap_new(), (IOStatePtr) StreamState__instance());
    IO_REGISTER(io_subsystem, KEYBOARD, Stream__heap_new(), (IOStatePtr) StreamState__heap_new());
    IO_REGISTER(io_subsystem, HELLO_STREAM, Stream__heap_new(), (IOStatePtr) StreamState__heap_new());
    IO_REGISTER(io_subsystem, BUSINFO_STREAM, Stream__heap_new(), (IOStatePtr) StreamState__heap_new());
    IO_REGISTER(io_subsystem, ROOT, &root_fs->file_io.base_io, (IOStatePtr) root_fs->state);

    match_result(ListenerRegisterResult, IOSubsystem__register_listeners(kernel),
        success_do_nothing(),
        error(code, message) { return Error(RegisterCode, message, IO_REGISTER_FAILED); }
    );
    return Success(RegisterCode, IO_OK);
}

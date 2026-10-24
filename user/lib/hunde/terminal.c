#include "native.h"
#include "io/client.h"
#include "sys/process.h"

_Static_assert(sizeof(IOTerminalAttributes) == 44, "Rust terminal attributes ABI mismatch");

static HundeIOCompletion HundeIOCompletion__new(Anything value)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_SUCCESS, .value = value};
}

static HundeIOCompletion HundeIOCompletion__new_error(int32_t code)
{
    return (HundeIOCompletion){.kind = HUNDE_ABI_ERROR, .error = RustNative__error(code).error};
}

static HundeIOCompletion RustTerminal__transfer(ByteCount count)
{
    return count < 0 ? HundeIOCompletion__new_error(count) : HundeIOCompletion__new(count);
}

HundeIOCompletion hunde_rust_terminal_open(HundeByteSpan service, uint64_t flags)
{
    if (service.length == 0 || service.length >= IO_PROTOCOL_DATA_SIZE)
    {
        return HundeIOCompletion__new_error(IO_INVALID_PATH);
    }
    BString name = BString__from_chars((const char *)service.bytes, service.length);
    match_result(
        IOClient__open_service(&name, &bstr(":backend:"), flags),
        success(handle)
        {
            return HundeIOCompletion__new(handle);
        },
        error(code, message)
        {
            return HundeIOCompletion__new_error(code);
        }
    );
    return HundeIOCompletion__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

HundeIOCompletion hunde_rust_terminal_read(int64_t handle, uint8_t * data, uint64_t length)
{
    return RustTerminal__transfer(IOClient__read(handle, data, length));
}

HundeIOCompletion hunde_rust_terminal_write(int64_t handle, const uint8_t * data, uint64_t length)
{
    return RustTerminal__transfer(IOClient__write(handle, data, length));
}

HundeStatusResult hunde_rust_terminal_close(int64_t handle)
{
    IOStatus status = IOClient__close(handle);
    return status == IO_OK ? HundeStatusResult__success() : RustNative__error(status);
}

HundeStatusResult hunde_rust_terminal_interrupt(uint64_t pid)
{
    ProcessSignalResult status = kill_process(pid, PROCESS_SIGNAL_INTERRUPT);
    return status == PROCESS_SIGNAL_OK ? HundeStatusResult__success() : RustNative__error(status);
}

#include <errno.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include "include/sys/ioctl.h"
#include "include/termios.h"

#include "../sys/io.h"
#include "posix_io.h"

_Static_assert(NCCS == IO_TERMINAL_CONTROL_CHARACTER_COUNT, "terminal control character ABI mismatch");
_Static_assert(ICRNL == IO_TERMINAL_INPUT_CR_TO_NL, "terminal input flag ABI mismatch");
_Static_assert(OPOST == IO_TERMINAL_OUTPUT_PROCESSING, "terminal output flag ABI mismatch");
_Static_assert(CS8 == IO_TERMINAL_CONTROL_CHARACTER_SIZE_8, "terminal control flag ABI mismatch");
_Static_assert(ICANON == IO_TERMINAL_LOCAL_CANONICAL, "terminal local flag ABI mismatch");
_Static_assert(ECHO == IO_TERMINAL_LOCAL_ECHO, "terminal echo flag ABI mismatch");
_Static_assert(B38400 == IO_TERMINAL_SPEED_38400, "terminal speed ABI mismatch");

static int PosixTerminal__error(IOStatus status)
{
    if (status == IO_UNKNOWN_HANDLE || status == IO_NOT_OPEN)
    {
        errno = EBADF;
    }
    else if (status == IO_OPERATION_NOT_SUPPORTED || status == IO_OPERATION_NOT_IMPLEMENTED)
    {
        errno = ENOTTY;
    }
    else
    {
        errno = EIO;
    }
    return -1;
}

static IOHandle PosixTerminal__handle(int descriptor)
{
    match_optional(
        PosixIO__terminal_path(descriptor),
        some(path)
        {
            return PosixIO__native_handle(descriptor);
        },
        none()
        {
            return IO_UNKNOWN_HANDLE;
        }
    );
}

int tcgetattr(int descriptor, struct termios * attributes)
{
    if (attributes == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    IOHandle handle = PosixTerminal__handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }
    match_result(
        IO__get_terminal_attributes(handle),
        success(native)
        {
            attributes->c_iflag = native.input_flags;
            attributes->c_oflag = native.output_flags;
            attributes->c_cflag = native.control_flags;
            attributes->c_lflag = native.local_flags;
            attributes->c_ispeed = native.input_speed;
            attributes->c_ospeed = native.output_speed;
            memcpy(attributes->c_cc, native.control_characters, NCCS);
            return 0;
        },
        error(code, message)
        {
            return PosixTerminal__error((IOStatus)code);
        }
    );
    errno = EIO;
    return -1;
}

int tcgetwinsize(int descriptor, struct winsize * size)
{
    if (size == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (PosixTerminal__handle(descriptor) < 0)
    {
        return -1;
    }
    errno = EOPNOTSUPP;
    return -1;
}

int tcsetattr(int descriptor, int action, const struct termios * attributes)
{
    if (attributes == NULL || (action != TCSANOW && action != TCSADRAIN))
    {
        errno = EINVAL;
        return -1;
    }
    IOHandle handle = PosixTerminal__handle(descriptor);
    if (handle < 0)
    {
        return -1;
    }
    IOTerminalAttributes native = {
        .input_flags = attributes->c_iflag,
        .output_flags = attributes->c_oflag,
        .control_flags = attributes->c_cflag,
        .local_flags = attributes->c_lflag,
        .input_speed = attributes->c_ispeed,
        .output_speed = attributes->c_ospeed,
    };
    memcpy(native.control_characters, attributes->c_cc, NCCS);
    match_result(
        IO__set_terminal_attributes(handle, native),
        success(status)
        {
            return status == IO_OK ? 0 : -1;
        },
        error(code, message)
        {
            return PosixTerminal__error((IOStatus)code);
        }
    );
    errno = EIO;
    return -1;
}

int tcsetwinsize(int descriptor, const struct winsize * size)
{
    if (size == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (PosixTerminal__handle(descriptor) < 0)
    {
        return -1;
    }
    errno = EOPNOTSUPP;
    return -1;
}

int ioctl(int descriptor, unsigned long request, ...)
{
    if (request == TIOCGWINSZ || request == TIOCSWINSZ)
    {
        va_list arguments;
        va_start(arguments, request);
        struct winsize * size = va_arg(arguments, struct winsize *);
        va_end(arguments);
        return request == TIOCGWINSZ ? tcgetwinsize(descriptor, size) : tcsetwinsize(descriptor, size);
    }
    errno = ENOSYS;
    return -1;
}

int tcdrain(int descriptor)
{
    return PosixTerminal__handle(descriptor) < 0 ? -1 : 0;
}

int tcflow(int descriptor, int action)
{
    if (action != TCOOFF && action != TCOON && action != TCIOFF && action != TCION)
    {
        errno = EINVAL;
        return -1;
    }
    if (PosixTerminal__handle(descriptor) < 0)
    {
        return -1;
    }
    errno = EOPNOTSUPP;
    return -1;
}

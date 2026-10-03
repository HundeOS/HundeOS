#include <errno.h>
#include <string.h>
#include "../../../user/lib/newlib/include/sys/ioctl.h"
#include "../../../user/lib/newlib/include/termios.h"

#include "unity.h"
#include "io/protocol.h"
#include "../../../user/lib/newlib/posix_io.h"
#include "../../../user/lib/sys/io.h"

static IOTerminalAttributes attributes;
static IOStatus next_status;
static IOTerminalAttributes written_attributes;

Optional(BString) PosixIO__terminal_path(int descriptor)
{
    if (descriptor == 7)
    {
        return Some(BString, bstr(":tty:"));
    }
    errno = descriptor == 8 ? ENOTTY : EBADF;
    return None(BString);
}

IOHandle PosixIO__native_handle(int descriptor)
{
    return descriptor == 7 ? IO_SESSION_HANDLE_BASE + 4 : IO_UNKNOWN_HANDLE;
}

IOTerminalAttributesResult IO__get_terminal_attributes(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 4, handle);
    return IOTerminalAttributesResult__new(next_status, attributes);
}

IOStatus IO__set_terminal_attributes(IOHandle handle, IOTerminalAttributes value)
{
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 4, handle);
    written_attributes = value;
    return next_status;
}

#include "../../../user/lib/newlib/termios.c"

void setUp(void)
{
    errno = 0;
    attributes = IOTerminalAttributes__default();
    next_status = IO_OK;
    written_attributes = (IOTerminalAttributes){0};
}

void tearDown(void)
{
}

void test_tcgetattr_reads_terminal_attributes(void)
{
    struct termios value = {0};

    TEST_ASSERT_EQUAL_INT(0, tcgetattr(7, &value));
    TEST_ASSERT_BITS(ICANON | ECHO, ICANON | ECHO, value.c_lflag);
    TEST_ASSERT_EQUAL_UINT8(3, value.c_cc[VINTR]);
    TEST_ASSERT_EQUAL_UINT8(1, value.c_cc[VMIN]);
    TEST_ASSERT_EQUAL_UINT(B38400, value.c_ispeed);
}

void test_tcsetattr_writes_complete_terminal_attributes(void)
{
    struct termios value = {
        .c_iflag = ICRNL,
        .c_oflag = OPOST,
        .c_cflag = CS8,
        .c_lflag = ISIG,
        .c_ispeed = B9600,
        .c_ospeed = B19200,
    };
    value.c_cc[VMIN] = 4;

    TEST_ASSERT_EQUAL_INT(0, tcsetattr(7, TCSADRAIN, &value));
    TEST_ASSERT_EQUAL_UINT32(ICRNL, written_attributes.input_flags);
    TEST_ASSERT_EQUAL_UINT32(ISIG, written_attributes.local_flags);
    TEST_ASSERT_EQUAL_UINT8(4, written_attributes.control_characters[VMIN]);
    TEST_ASSERT_EQUAL_UINT32(B9600, written_attributes.input_speed);
    TEST_ASSERT_EQUAL_UINT32(B19200, written_attributes.output_speed);
}

void test_termios_rejects_invalid_descriptors_arguments_and_actions(void)
{
    struct termios value = {0};

    TEST_ASSERT_EQUAL_INT(-1, tcgetattr(8, &value));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcgetattr(7, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcsetattr(7, TCSAFLUSH, &value));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_tcgetwinsize_reports_unsupported_without_changing_the_size(void)
{
    struct winsize size = {
        .ws_row = 24,
        .ws_col = 80,
        .ws_xpixel = 640,
        .ws_ypixel = 480,
    };

    TEST_ASSERT_EQUAL_INT(-1, tcgetwinsize(7, &size));
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);
    TEST_ASSERT_EQUAL_UINT16(24, size.ws_row);
    TEST_ASSERT_EQUAL_UINT16(80, size.ws_col);
    TEST_ASSERT_EQUAL_UINT16(640, size.ws_xpixel);
    TEST_ASSERT_EQUAL_UINT16(480, size.ws_ypixel);
}

void test_tcgetwinsize_validates_output_and_descriptor(void)
{
    struct winsize size = {0};

    TEST_ASSERT_EQUAL_INT(-1, tcgetwinsize(7, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcgetwinsize(8, &size));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcgetwinsize(9, &size));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
}

void test_tcsetwinsize_reports_unsupported_after_validating_arguments(void)
{
    const struct winsize size = {
        .ws_row = 24,
        .ws_col = 80,
    };

    TEST_ASSERT_EQUAL_INT(-1, tcsetwinsize(7, &size));
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcsetwinsize(7, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcsetwinsize(8, &size));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcsetwinsize(9, &size));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
}

void test_ioctl_dispatches_window_size_requests(void)
{
    struct winsize size = {
        .ws_row = 24,
        .ws_col = 80,
    };

    TEST_ASSERT_EQUAL_INT(-1, ioctl(7, TIOCGWINSZ, &size));
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);
    TEST_ASSERT_EQUAL_UINT16(24, size.ws_row);
    TEST_ASSERT_EQUAL_UINT16(80, size.ws_col);

    errno = 0;
    TEST_ASSERT_EQUAL_INT(-1, ioctl(7, TIOCSWINSZ, &size));
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);
}

void test_ioctl_rejects_unknown_requests(void)
{
    struct winsize size = {0};

    TEST_ASSERT_EQUAL_INT(-1, ioctl(7, 0, &size));
    TEST_ASSERT_EQUAL_INT(ENOSYS, errno);
}

void test_tcflow_reports_unsupported_after_validating_terminal(void)
{
    TEST_ASSERT_EQUAL_INT(-1, tcflow(7, TCOON));
    TEST_ASSERT_EQUAL_INT(EOPNOTSUPP, errno);
    TEST_ASSERT_EQUAL_INT(-1, tcflow(7, 99));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

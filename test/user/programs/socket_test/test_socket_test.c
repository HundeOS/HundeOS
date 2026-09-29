#include <stdarg.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"
#include "io/client.h"

IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem request in socket program");
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem link");
    return IO_OPERATION_NOT_SUPPORTED;
}

#include "../../../../common_includes/syscalls/calls.h"
#include "../../../../common_includes/syscalls/io.h"

typedef struct RecordedCall
{
    SystemCall call;
    Anything arg1;
    Anything arg2;
    Anything arg3;
    Anything arg4;
    Anything arg5;
} RecordedCall;

static RecordedCall calls[16];
static uint64_t call_count;
static Anything results[16];
static uint64_t result_count;
static uint64_t result_pos;
static IOPathRequest last_open_request;
static char last_write[128];
static char output[1024];
static uint64_t output_position;

#define main socket_test_main_under_test
#include "../../../../user/programs/socket_test/main.c"
#include "../../../../user/lib/sys/io.c"

static void SocketTestTest__queue_result(Anything result)
{
    results[result_count] = result;
    result_count++;
}

void setUp(void)
{
    memset(calls, 0, sizeof(calls));
    call_count = 0;
    result_count = 0;
    result_pos = 0;
    last_open_request = (IOPathRequest){0};
    last_write[0] = '\0';
    output_position = 0;
    output[0] = '\0';
}

void tearDown(void)
{
}

static Anything record_operation(SystemCall call, Anything arg1, Anything arg2, Anything arg3, Anything arg4,
                                 Anything arg5)
{
    calls[call_count] = (RecordedCall){
        .call = call,
        .arg1 = arg1,
        .arg2 = arg2,
        .arg3 = arg3,
        .arg4 = arg4,
        .arg5 = arg5,
    };
    call_count++;

    if (call == IO_CALL && arg1 == IO_OPEN)
    {
        last_open_request = *((IOPathRequestPtr)arg2);
    }

    if (call == IO_CALL && arg1 == IO_WRITE)
    {
        memcpy(last_write, (void *)arg3, (size_t)arg4);
        last_write[arg4] = '\0';
    }

    if (call == IO_CALL && arg1 == IO_READ)
    {
        char * buffer = (char *)arg3;
        const char * text = "pong";
        if (arg2 == IO_SESSION_HANDLE_BASE + 22)
        {
            text = "ping";
        }
        size_t len = strlen(text);
        memcpy(buffer, text, len);
        return (Anything)len;
    }

    Anything result = 0;
    if (result_pos < result_count)
    {
        result = results[result_pos];
        result_pos++;
    }
    return result;
}

Anything syscall(SystemCall call, Anything arg1, Anything arg2, Anything arg3, Anything arg4, Anything arg5)
{
    TEST_FAIL_MESSAGE("Socket operations must not use IO syscalls");
    return -1;
}

IOHandle IOClient__open(ConstBStringPtr path, Flags flags)
{
    IOPathRequest request = IOPathRequest__new(path, flags);
    return record_operation(IO_CALL, IO_OPEN, (Anything)&request, 0, 0, 0);
}

IOHandle IOClient__accept(IOHandle handle)
{
    return record_operation(IO_CALL, IO_ACCEPT, handle, 0, 0, 0);
}

IOStatus IOClient__close(IOHandle handle)
{
    return record_operation(IO_CALL, IO_CLOSE, handle, 0, 0, 0);
}

IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid)
{
    TEST_FAIL_MESSAGE("Unexpected terminal control");
    return IO_OPERATION_NOT_SUPPORTED;
}

ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size)
{
    return record_operation(IO_CALL, IO_READ, handle, (Anything)data, size, 0);
}

ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size)
{
    return record_operation(IO_CALL, IO_WRITE, handle, (Anything)data, size, 0);
}

IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem seek");
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

IOReply IOClient__watch_file(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem watch");
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count)
{
    TEST_FAIL_MESSAGE("Unexpected filesystem wait");
    return IOReply__new(IO_OPERATION_NOT_SUPPORTED, 0);
}

void IO__print(ConstBStringPtr message)
{
    StringPos written = BString__copy_to(message, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t)written;
    }
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString formatted = BString__vsprintf(format, args);
    va_end(args);
    StringPos written = BString__copy_to(&formatted, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t)written;
    }
    return (int)written;
}

void test_socket_test_connect_opens_socket_writes_message_and_reads_reply(void)
{
    char * argv[] = {"socket_test", "connect", "socket://proc/demo", "hello"};
    SocketTestTest__queue_result(IO_SESSION_HANDLE_BASE + 21);
    SocketTestTest__queue_result(5);
    SocketTestTest__queue_result(IO_OK);

    TEST_ASSERT_EQUAL_INT(0, socket_test_main_under_test(4, argv));

    TEST_ASSERT_EQUAL_UINT64(4, call_count);
    TEST_ASSERT_EQUAL(IO_CALL, calls[0].call);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[0].arg1);
    TEST_ASSERT_EQUAL_BSTRING(bstr("socket://proc/demo"), last_open_request.path);
    TEST_ASSERT_EQUAL(IO_OPEN_READ | IO_OPEN_WRITE, last_open_request.flags);
    TEST_ASSERT_EQUAL(IO_WRITE, calls[1].arg1);
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 21, calls[1].arg2);
    TEST_ASSERT_EQUAL_STRING("hello", last_write);
    TEST_ASSERT_EQUAL(IO_READ, calls[2].arg1);
    TEST_ASSERT_EQUAL(IO_CLOSE, calls[3].arg1);
    TEST_ASSERT_EQUAL_STRING("socket_test: reply pong\n", output);
}

void test_socket_test_listen_binds_accepts_reads_and_replies(void)
{
    char * argv[] = {"socket_test", "listen", "socket://proc/demo"};
    SocketTestTest__queue_result(IO_SESSION_HANDLE_BASE + 11);
    SocketTestTest__queue_result(IO_SESSION_HANDLE_BASE + 22);
    SocketTestTest__queue_result(4);
    SocketTestTest__queue_result(IO_OK);
    SocketTestTest__queue_result(IO_OK);

    TEST_ASSERT_EQUAL_INT(0, socket_test_main_under_test(3, argv));

    TEST_ASSERT_EQUAL_UINT64(6, call_count);
    TEST_ASSERT_EQUAL(IO_OPEN, calls[0].arg1);
    TEST_ASSERT_EQUAL_BSTRING(bstr("socket://proc/demo"), last_open_request.path);
    TEST_ASSERT_EQUAL(IO_OPEN_BIND | IO_OPEN_LISTEN, last_open_request.flags);
    TEST_ASSERT_EQUAL(IO_ACCEPT, calls[1].arg1);
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 11, calls[1].arg2);
    TEST_ASSERT_EQUAL(IO_READ, calls[2].arg1);
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 22, calls[2].arg2);
    TEST_ASSERT_EQUAL(IO_WRITE, calls[3].arg1);
    TEST_ASSERT_EQUAL_STRING("pong", last_write);
    TEST_ASSERT_EQUAL(IO_CLOSE, calls[4].arg1);
    TEST_ASSERT_EQUAL(IO_CLOSE, calls[5].arg1);
    TEST_ASSERT_EQUAL_STRING("socket_test: listening socket://proc/demo\nsocket_test: received ping\n", output);
}

void test_socket_test_prints_usage_for_bad_mode(void)
{
    char * argv[] = {"socket_test", "wat"};

    TEST_ASSERT_EQUAL_INT(1, socket_test_main_under_test(2, argv));
    TEST_ASSERT_EQUAL_STRING("socket_test: usage socket_test listen [socket://path]\n"
                             "socket_test: usage socket_test connect [socket://path] [message]\n",
                             output);
}

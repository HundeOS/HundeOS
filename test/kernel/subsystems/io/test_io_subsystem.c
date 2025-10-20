#include "unity.h"
#include "../../../../kernel/subsystems/io/io_subsystem.h"

static const int FAKE_HANDLE = 100;
#define MAX_READ 100

static bool opened = false;
static bool closed = false;
static bool written = false;
static bool read = false;

static BufferPtr write_buffer = NULL;

typedef struct FakeIOState
{
    IOState state;
    IOHandleArray handles;
};

void reset_state()
{
    opened = false;
    closed = false;
    written = false;
    read = false;
    write_buffer = NULL;
}

static Result(IOHandle) fake_open(IOStatePtr state, KString path, Flags flags)
{
    opened = true;
    IOHandleArray__push(&state->handles, FAKE_HANDLE);
    return Success(IOHandle, FAKE_HANDLE);
}

static Result(BufferPtr) fake_read(IOStatePtr state, IOHandle handle, size_t size)
{
    read = true;
    state->buffer = Buffer__heap_new();

    Buffer__push(state->buffer, 1);
    Buffer__push(state->buffer, 2);
    Buffer__push(state->buffer, 3);
    Buffer__push(state->buffer, 4);

    return Success(BufferPtr, state->buffer);
}

static Result(ByteCount) fake_write(IOStatePtr state, IOHandle handle, BufferPtr buffer, size_t size)
{
    written = true;
    write_buffer = buffer;
    return Success(ByteCount, size);
}

static Result(IOHandle) fake_close(IOStatePtr state, IOHandle handle)
{
    closed = true;
    if (!opened)
    {
        return Error(IOHandle, kstring("not opened"), IO_NOT_OPEN);
    }
    return Success(IOHandle, handle);
}

#define STUB_IO \
    (IO) { \
        .read = fake_read, \
        .write = fake_write,\
        .open = fake_open, \
        .close = fake_close, \
    }

void  setUp(void)
{
    reset_state();
}

void test_io_subsystem_registration_and_open()
{
    IO test_io = STUB_IO;

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, path, 0))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
}

void test_io_open_and_read()
{
    IO test_io = STUB_IO;

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;
    IOHandle handle = -1;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, path, 0))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Char expected[] = {1, 2, 3, 4};
    ResultMatch(BufferPtr, IOSubsystem__read(&system, handle, MAX_READ));
        IsSuccess(buffer,
        {
            TEST_ASSERT_EQUAL_INT(4, buffer->len);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, buffer->data, buffer->len);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(read);
}

void test_io_open_and_write()
{
    IO test_io = STUB_IO;

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;
    IOHandle handle = -1;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, path, 0))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Buffer buffer = Buffer__new();

    ResultMatch(ByteCount, IOSubsystem__write(&system, handle, &buffer, 10));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(10, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(written);
    TEST_ASSERT_EQUAL(&buffer, write_buffer);
}

void test_io_write_when_not_open()
{
    IO test_io = STUB_IO;

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;
    IOHandle handle = -1;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    Buffer buffer = Buffer__new();

    ResultMatch(ByteCount, IOSubsystem__write(&system, handle, &buffer, 10));
        IsSuccess(byte_count,
        {
            TEST_FAIL();
        });
    End

    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_NULL(write_buffer);
}

void test_io_read_when_not_open()
{
    k_println("starting test");
    IO test_io = STUB_IO;

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;
    IOHandle handle = -1;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(BufferPtr, IOSubsystem__read(&system, handle, MAX_READ));
        IsSuccess(buffer,
        {
            TEST_FAIL();
        });
    End

    TEST_ASSERT_FALSE(read);
}

void test_io_subsystem_registration_and_close()
{
    IO test_io = STUB_IO;
    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, path, 0))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, FAKE_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_close_disposes_handle()
{
    IO test_io = STUB_IO;
    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOState state = NEW_IO_STATE;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, path, 0))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, FAKE_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, FAKE_HANDLE))
        IsSuccess(result, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(closed);
}

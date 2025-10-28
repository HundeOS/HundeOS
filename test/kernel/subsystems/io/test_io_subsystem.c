#include "unity.h"
#include "../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../stubs.h"

static const int FAKE_HANDLE = 100;
#define MAX_READ 100

static bool opened = false;
static bool closed = false;
static bool written = false;
static bool read = false;

static DataPtr write_data = NULL;

static Process PROC = STUB_PROCESS;
static IOState STATE = NEW_IO_STATE;

static Flags FLAGS = 1234;

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
    write_data = NULL;
}

static Result(IOHandle) fake_open(IOStatePtr state_ptr, KString path, Flags flags)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FLAGS, flags);

    IOHandleArray__push(&state_ptr->handles, FAKE_HANDLE);
    opened = true;
    return Success(IOHandle, FAKE_HANDLE);
}

static Result(IOBufferPtr) fake_read(IOStatePtr state_ptr, IOHandle handle, size_t size)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    IOBufferPtr buffer = IOBuffer__heap_new();
    __d_printf(DEBUG, "reading %p\n", buffer);
    IOBuffer__push(buffer, 1);
    IOBuffer__push(buffer, 2);
    IOBuffer__push(buffer, 3);
    IOBuffer__push(buffer, 4);
    __d_printf(FOCUS, "in stub  %i %i %i\n", buffer->read_pos, buffer->write_pos, buffer->buffer_size);
    read = true;
    return Success(IOBufferPtr, buffer);
}

static Result(ByteCount) fake_write(IOStatePtr state_ptr, IOHandle handle, DataPtr data, size_t size)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    written = true;
    write_data = data;
    return Success(ByteCount, size);
}

static Result(IOHandle) fake_close(IOStatePtr state_ptr, IOHandle handle)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);

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

static IO TEST_IO = STUB_IO;

void setUp(void)
{
    reset_state();
}

void test_io_subsystem_registration_and_open()
{
    IOSubsystem system = NEW_IO_SUBSYSTEM;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, &PROC, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
}

void test_io_open_and_read()
{
    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, &PROC, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Char expected[] = {1, 2, 3, 4};
    ResultMatch(ByteCount, IOSubsystem__read(&system, &PROC, handle, &data, MAX_READ));
        IsSuccess(count,
        {
            TEST_ASSERT_EQUAL_INT(4, count);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(read);
}

void test_io_open_and_write()
{
    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, &PROC, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__write(&system, &PROC, handle, &data, MAX_READ));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(MAX_READ, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(written);
    TEST_ASSERT_EQUAL(&data, write_data);
}

void test_io_write_when_not_open()
{
    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__write(&system, &PROC, handle, &data, 10));
        IsSuccess(byte_count,
        {
            TEST_FAIL();
        });
    End

    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_NULL(write_data);
}

void test_io_read_when_not_open()
{
    k_println("starting test");
    Data data[MAX_READ] = {0};

    IOSubsystem system = NEW_IO_SUBSYSTEM;
    IOHandle handle = -1;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__read(&system, &PROC, handle, &data, MAX_READ));
        IsSuccess(count,
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

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, &PROC, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, &PROC, FAKE_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_close_disposes_handle()
{
    IOSubsystem system = NEW_IO_SUBSYSTEM;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, &PROC, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, &PROC, FAKE_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(FAKE_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, &PROC, FAKE_HANDLE))
        IsSuccess(result, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(closed);
}

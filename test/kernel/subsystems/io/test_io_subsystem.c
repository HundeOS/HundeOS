#include "unity.h"
#include "../../stubs.h"
#include "../../stubs/process.h"
#include "../../fixtures/kernel.h"

static const IOInternalHandle FAKE_HANDLE =            0x00000000000000FF;
static const IOHandle EXPECTED_OPEN_HANDLE =           0x00000002000000FF; // upper 32bits hold the device handle
static const DeviceHandle EXPECTED_DEVICE_HANDLE =     0x00000002; // upper 32bits hold the device handle
#define MAX_READ 100

static bool opened = false;
static bool closed = false;
static bool written = false;
static bool read = false;

static CPUID cpu = 1;

static KernelPtr kernel = NULL;
static DataPtr write_data = NULL;
static IOBufferPtr buffer = NULL;

static ProcessContextPtr context = (ProcessContextPtr) 0x12345;

static Process PROC = STUB_PROCESS(1);
static Optional(ProcessPtr) proc = Some(ProcessPtr, &PROC);

static Flags FLAGS = 1234;

static Char expected[] = {1, 2, 3, 4};

static KString path = KSTRING_NEW(":test:");
static KString invalid_path = KSTRING_NEW("/sss");

#define NEW_FAKE_IO_STATE \
    (FakeIOState) { \
        .state = NEW_IO_STATE, \
        .handles = EmptyArray(IOHandleArray), \
        .parked_context = None(IOContext), \
     } \

typedef struct FakeIOState
{
    IOState state;
    IOHandleArray handles;
    Optional(IOContext) parked_context;
} FakeIOState;

typedef FakeIOState * FakeIOStatePtr;

static FakeIOState STATE = NEW_FAKE_IO_STATE;

void reset_state()
{
    opened = false;
    closed = false;
    written = false;
    read = false;
    write_data = NULL;
    PROC = STUB_PROCESS(cpu);
    STATE = NEW_FAKE_IO_STATE;
    buffer = IOBuffer__heap_new();
    clear_deferred_syscall_result();
}

static Result(IOInternalHandle) fake_open(IOStatePtr state_ptr, KString path, Flags flags)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FLAGS, flags);

    IOHandleArray__push(&state_ptr->handles, FAKE_HANDLE);
    opened = true;
    return Success(IOInternalHandle, FAKE_HANDLE);
}

static Result(IOBufferPtr) fake_read(IOStatePtr state_ptr, IOInternalHandle handle, size_t size)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    read = true;
    if (IOBuffer__item_count(buffer) == 0)
    {
        return Error(IOBufferPtr, kstring("no data"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, buffer);
}

static Result(ByteCount) fake_write(IOStatePtr state_ptr, IOInternalHandle handle, DataPtr data, size_t size)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    written = true;
    write_data = data;
    return Success(ByteCount, size);
}

static Result(IOInternalHandle) fake_close(IOStatePtr state_ptr, IOInternalHandle handle)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FAKE_HANDLE, handle);

    closed = true;
    if (!opened)
    {
        return Error(IOInternalHandle, kstring("not opened"), IO_NOT_OPEN);
    }
    return Success(IOInternalHandle, handle);
}

static Result(ParkStatus) fake_unpark(IOStatePtr state)
{
    FakeIOStatePtr self = (FakeIOStatePtr) state;
    self->parked_context = None(IOContext);
    return Success(ParkStatus, IO_UNPARK_OK);
}

static Result(ParkStatus) fake_park(IOStatePtr state, IOContext parked_context)
{
    FakeIOStatePtr self = (FakeIOStatePtr) state;
    self->parked_context = Some(IOContext, parked_context);
    return Success(ParkStatus, IO_PARK_OK);
}

#define STUB_IO \
    (IO) { \
        .read = fake_read, \
        .write = fake_write,\
        .open = fake_open, \
        .close = fake_close, \
        .park = fake_park, \
        .unpark = fake_unpark, \
    }

static IO TEST_IO = STUB_IO;

void setUp(void)
{
    kernel = init_test_kernel();
    reset_state();

    Scheduler__schedule(kernel->scheduler, &PROC);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&kernel->io_subsystem, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

}

void data_available()
{
    IOBuffer__push(buffer, 1);
    IOBuffer__push(buffer, 2);
    IOBuffer__push(buffer, 3);
    IOBuffer__push(buffer, 4);
}

void data_available_interrupt(IOSubsystemPtr io_subsystem)
{
    Match(IOContext, STATE.parked_context)
        IsSome(parked_context,
        {
            IOBuffer__push(buffer, 1);
            IOBuffer__push(buffer, 2);
            IOBuffer__push(buffer, 3);
            IOBuffer__push(buffer, 4);
            IOSubsystem__data_arrived(io_subsystem, parked_context);
        })
        IsNone(
        {
            TEST_FAIL();
        })
    End
}

IOContext io_context(Optional(IOHandle) handle, Optional(ExternalBuffer) ext_buffer)
{
    KernelContext kernel_context = KernelContext__new(proc, kernel->scheduler);
    ;
    return IOContext__new(kernel_context, &kernel->io_subsystem, handle, ext_buffer);
}

IOContext open_context()
{
    return io_context(None(IOHandle), None(ExternalBuffer));
}

IOContext close_context(IOHandle handle)
{
    return io_context(Some(IOHandle, handle), None(ExternalBuffer));
}

IOContext rw_context(IOHandle handle, DataPtr data, size_t size)
{
    ExternalBuffer ext_buffer = ExternalBuffer__new(data, size);
    return io_context(Some(IOHandle, handle), Some(ExternalBuffer, ext_buffer));
}

void test_io_subsystem_registration_and_open()
{
    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
}

void test_io_open_and_read()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    data_available();

    ResultMatch(ByteCount, IOSubsystem__read(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
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

void test_io_open_path_not_found()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), invalid_path, FLAGS))
        IsSuccess(result, TEST_FAIL());
        // IsError(code, message, TEST_FAIL());
    End
}

void test_io_open_and_write()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    __d_println(DEBUG, "opening %p\n");
    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    __d_println(DEBUG, "writing %p\n");
    ResultMatch(ByteCount, IOSubsystem__write(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(MAX_READ, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(written);
    TEST_ASSERT_EQUAL(&data, write_data);
}

void test_io_open_and_read_when_no_data_ready_parks_the_process()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__read(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(read);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, PROC.state);

    ProcessPtr current = UnwrapPanic(ProcessPtr, Scheduler__current_process(kernel->scheduler, cpu));

    TEST_ASSERT_NOT_EQUAL(current, &PROC);
}

void test_io_open_and_read_when_no_data_ready_parks_the_process_and_unparks_when_data_arrives()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__read(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, PROC.state);

    ProcessPtr current = UnwrapPanic(ProcessPtr, Scheduler__current_process(kernel->scheduler, cpu));
    data_available_interrupt(&kernel->io_subsystem);
    Scheduler__preempt(kernel->scheduler, cpu, NULL);

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, PROC.state);

    ResultMatch(Anything, deferred_syscall_result())
        IsSuccess(count, TEST_ASSERT_EQUAL(4, count));
        IsError(code, message,
        {
            k_println(cstr(message));
            TEST_FAIL();
        });
    End

    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, data, 4);
    TEST_ASSERT_NOT_EQUAL(current, &PROC);
}

void test_io_write_when_not_open()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(ByteCount, IOSubsystem__write(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(byte_count,
        {
            TEST_FAIL();
        });
        IsError(code, message,
        {
            TEST_ASSERT_EQUAL(IO_NOT_OPEN, code);
        });
    End

    TEST_ASSERT_FALSE(written);
    TEST_ASSERT_NULL(write_data);
}

void test_io_read_when_not_open()
{
    Data data[MAX_READ] = {0};

    IOHandle handle = -1;

    ResultMatch(ByteCount, IOSubsystem__read(&kernel->io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(count,
        {
            TEST_FAIL();
        });
        IsError(code, message,
        {
            TEST_ASSERT_EQUAL(IO_NOT_OPEN, code);
        });
    End

    TEST_ASSERT_FALSE(read);
}

void test_io_subsystem_registration_and_close()
{
    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&kernel->io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_close_disposes_handle()
{
    ResultMatch(IOHandle, IOSubsystem__open(&kernel->io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&kernel->io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&kernel->io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsSome(val, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(closed);
}

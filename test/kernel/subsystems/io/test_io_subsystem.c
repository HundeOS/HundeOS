#include "unity.h"
#include "../../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../../kernel/scheduler/scheduler.h"
#include "../../stubs.h"
#include "../../stubs/process.h"
#include "../../../../kernel/bin/embedded_binaries.h"

static const IOInternalHandle FAKE_HANDLE =            0x00000000000000FF;
static const IOHandle EXPECTED_OPEN_HANDLE =           0x00000001000000FF; // upper 32bits hold the device handle
static const DeviceHandle EXPECTED_DEVICE_HANDLE =     0x00000001; // upper 32bits hold the device handle
#define MAX_READ 100

static bool opened = false;
static bool closed = false;
static bool written = false;
static bool read = false;

static CPUID cpu = 1;

static DataPtr write_data = NULL;
static IOBufferPtr buffer = NULL;
static SchedulerPtr scheduler = NULL;

static ProcessContextPtr context = (ProcessContextPtr) 0x12345;

static Process PROC = STUB_PROCESS(1);
static Optional(ProcessPtr) proc = Some(ProcessPtr, &PROC);

IOSubsystem system = NEW_IO_SUBSYSTEM(NULL);

static Flags FLAGS = 1234;

static Char expected[] = {1, 2, 3, 4};

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

SchedulerPtr setup_scheduler()
{
    Elf idle = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf) {})
    scheduler = Scheduler__heap_new();
    Scheduler__init(scheduler, idle);
    Scheduler__start(scheduler, cpu);
    Scheduler__schedule(scheduler, &PROC);
    Scheduler__preempt(scheduler, cpu, context);

    return scheduler;
}

void setUp(void)
{
    reset_state();
    system = NEW_IO_SUBSYSTEM(setup_scheduler());
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

void test_io_subsystem_registration_and_open()
{
    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
        IsSuccess(handle, TEST_ASSERT_EQUAL_UINT(EXPECTED_DEVICE_HANDLE, handle));
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
}

void test_io_open_and_read()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    data_available();

    IOContext context = (IOContext) {
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = MAX_READ,
        .io_subsystem = &system,
    };

    ResultMatch(ByteCount, IOSubsystem__read(&system, context));
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
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    __d_println(DEBUG, "reg %p\n");
    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    __d_println(DEBUG, "opening %p\n");
    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContext context = (IOContext) {
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = MAX_READ,
        .io_subsystem = &system,
    };

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    __d_println(DEBUG, "writing %p\n");
    ResultMatch(ByteCount, IOSubsystem__write(&system, context));
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

    KString path = KString__new("/dev/test");

    __d_println(DEBUG, "reg %p\n");
    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    __d_println(DEBUG, "opening %p\n");
    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContext context = (IOContext) {
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = MAX_READ,
        .io_subsystem = &system,
    };

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    __d_println(DEBUG, "reading %p\n");
    ResultMatch(ByteCount, IOSubsystem__read(&system, context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(read);

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, PROC.state);

    ProcessPtr current = UnwrapPanic(ProcessPtr, Scheduler__current_process(scheduler, cpu));

    TEST_ASSERT_NOT_EQUAL(current, &PROC);
}

void test_io_open_and_read_when_no_data_ready_parks_the_process_and_unparks_when_data_arrives()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    KString path = KString__new("/dev/test");

    __d_println(DEBUG, "reg %p\n");
    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    __d_println(DEBUG, "opening %p\n");
    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsNone(TEST_FAIL());
    End

    IOContext context = (IOContext){
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = MAX_READ,
        .io_subsystem = &system,
    };

    __d_println(DEBUG, "reading %p\n");
    ResultMatch(ByteCount, IOSubsystem__read(&system, context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, PROC.state);

    ProcessPtr current = UnwrapPanic(ProcessPtr, Scheduler__current_process(scheduler, cpu));
    data_available_interrupt(&system);
    Scheduler__preempt(scheduler, cpu, NULL);

    ResultMatch(Anything, deferred_syscall_result())
        IsSuccess(count, TEST_ASSERT_EQUAL(4, count));
        IsError(code, message,
        {
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

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    IOContext context = (IOContext){
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = 10,
        .io_subsystem = &system,
    };

    ResultMatch(ByteCount, IOSubsystem__write(&system, context));
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

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
        IsSuccess(handle, TEST_ASSERT_EQUAL(EXPECTED_DEVICE_HANDLE, handle));
    End

    IOContext context = (IOContext){
        .proc = proc,
        .handle = handle,
        .buffer = data,
        .size = MAX_READ,
        .io_subsystem = &system,
    };

    ResultMatch(ByteCount, IOSubsystem__read(&system, context));
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
    IO test_io = STUB_IO;

    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &test_io, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, proc, EXPECTED_OPEN_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_close_disposes_handle()
{
    KString path = KString__new("/dev/test");

    ResultMatch(DeviceHandle, IOSubsystem__register_device(&system, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(&system, proc, path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, proc, EXPECTED_OPEN_HANDLE))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(&system, proc, EXPECTED_OPEN_HANDLE))
        IsSuccess(result, TEST_FAIL());
    End

    Match(ArrayIndex, ProcessHandles__find_item(&PROC.handles, EXPECTED_OPEN_HANDLE));
        IsSome(val, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(closed);
}

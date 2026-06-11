#include "unity.h"
#include "../../stubs.h"
#include "../../stubs/process.h"
#include "../../fixtures/kernel.h"
#include "../../../../kernel/subsystems/message/subsystem_codes.h"
#include "../../../../kernel/subsystems/io/console/console.h"

void BasicIO__reset_output();
const char * BasicIO__output();

static const IOInternalHandle INTERNAL_HANDLE =        0x00000000000000FF;
static const IOHandle EXPECTED_OPEN_HANDLE =           0x00000002000000FF; // upper 32bits hold the device handle
static const DeviceHandle EXPECTED_DEVICE_HANDLE =     0x00000002; // upper 32bits hold the device handle
#define MAX_READ 100

static bool opened = false;
static bool closed = false;
static bool written = false;
static bool read = false;

static IOHandle closed_handle = -1;

static const ProcessID PID = 1;
static const CPUID cpu = PID;

static KernelPtr kernel = NULL;
static IOSubsystemPtr io_subsystem = NULL;

static DataPtr write_data = NULL;
static IOBufferPtr buffer = NULL;

static ProcessContextPtr context = (ProcessContextPtr) 0x12345;

static Process PROC = STUB_PROCESS(PID);
static Optional(ProcessPtr) proc = Some(ProcessPtr, &PROC);
static Optional(ProcessID) pid = Some(ProcessID, PID);

static MessagePtr io_message = NULL;

static Flags FLAGS = 1234;

static Char expected[] = {1, 2, 3, 4};

static KString path = KSTRING_NEW(":test:");
static KString console_path = KSTRING_NEW(":console:");
static KString invalid_path = KSTRING_NEW("/sss");

#define NEW_FAKE_IO_STATE \
    (FakeIOState) { \
        .state = NEW_IO_STATE, \
        .handles = EmptyArray(IOHandleArray), \
        .parked_context = None(IOContextPtr), \
     } \

typedef struct FakeIOState
{
    IOState state;
    IOHandleArray handles;
    Optional(IOContextPtr) parked_context;
} FakeIOState;

typedef FakeIOState * FakeIOStatePtr;

static FakeIOState STATE = NEW_FAKE_IO_STATE;

void reset_state()
{
    opened = false;
    closed = false;
    written = false;
    read = false;

    closed_handle = -1;

    write_data = NULL;
    PROC = STUB_PROCESS(PID);
    STATE = NEW_FAKE_IO_STATE;
    buffer = IOBuffer__heap_new();
    clear_deferred_syscall_result();
}

static Result(IOInternalHandle) fake_open(IOStatePtr state_ptr, KString path, Flags flags)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(FLAGS, flags);

    IOHandleArray__push(&state_ptr->handles, INTERNAL_HANDLE);
    opened = true;
    return Success(IOInternalHandle, INTERNAL_HANDLE);
}

static Result(IOBufferPtr) fake_read(IOStatePtr state_ptr, IOInternalHandle handle, size_t size)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);
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
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    written = true;
    write_data = data;
    return Success(ByteCount, size);
}

static Result(IOInternalHandle) fake_close(IOStatePtr state_ptr, IOInternalHandle handle)
{
    TEST_ASSERT_EQUAL(&STATE, state_ptr);
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);

    closed = true;
    closed_handle = handle;

    if (!opened)
    {
        return Error(IOInternalHandle, kstring("not opened"), IO_NOT_OPEN);
    }
    return Success(IOInternalHandle, handle);
}

static Result(ParkStatus) fake_unpark(IOStatePtr state)
{
    FakeIOStatePtr self = (FakeIOStatePtr) state;
    self->parked_context = None(IOContextPtr);
    return Success(ParkStatus, IO_UNPARK_OK);
}

static Result(ParkStatus) fake_park(IOStatePtr state, IOContextPtr parked_context)
{
    FakeIOStatePtr self = (FakeIOStatePtr) state;
    self->parked_context = Some(IOContextPtr, parked_context);
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


void io_available_callback(KernelListenerPtr listener, MessagePtr message)
{
    io_message = message;
}

void setUp(void)
{
    kernel = init_test_kernel();
    io_subsystem = kernel->io_subsystem;
    reset_state();

    io_message = NULL;

    Scheduler__schedule(kernel->scheduler, &PROC);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    Listener listener = KernelListener__new(kernel, SOURCE_IO, io_available_callback);
    MessageBroker__register_listener(io_subsystem->message_broker, listener);

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ListenerRegisterResult, IOSubsystem__register_listeners(kernel))
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

void data_available_interrupt(IOSubsystemPtr io_sub)
{
    Match(IOContextPtr, STATE.parked_context)
        IsSome(parked_context,
        {
            IOBuffer__push(buffer, 1);
            IOBuffer__push(buffer, 2);
            IOBuffer__push(buffer, 3);
            IOBuffer__push(buffer, 4);
            IOSubsystem__data_arrived(io_sub, parked_context);
        })
        IsNone(
        {
            TEST_FAIL();
        })
    End
}

IOContextPtr io_context(Optional(IOHandle) handle, Optional(ExternalBuffer) ext_buffer)
{
    KernelContext kernel_context = KernelContext__new(pid, Some(SchedulerPtr, kernel->scheduler));
    ;
    return IOContext__heap_new(kernel_context, Some(IOSubsystemPtr, io_subsystem), handle, ext_buffer);
}

IOContextPtr open_context()
{
    return io_context(None(IOHandle), None(ExternalBuffer));
}

IOContextPtr close_context(IOHandle handle)
{
    return io_context(Some(IOHandle, handle), None(ExternalBuffer));
}

IOContextPtr rw_context(IOHandle handle, DataPtr data, size_t size)
{
    ExternalBuffer ext_buffer = ExternalBuffer__new(data, size);
    return io_context(Some(IOHandle, handle), Some(ExternalBuffer, ext_buffer));
}

void test_io_subsystem_registration_and_open()
{
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(opened);
}

void test_io_open_and_read()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    data_available();

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, rw_context(handle, data, MAX_READ)));
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

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), invalid_path, FLAGS))
        IsSuccess(result, TEST_FAIL());
    End
}

void test_io_open()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&io_subsystem->process_handles, PID));
        IsNone(TEST_FAIL());
        IsSome(handles,
        {
            Match(ArrayIndex, PIDHandles__find_item(handles, EXPECTED_OPEN_HANDLE))
                IsNone(TEST_FAIL())
            End
        })
    End

}

void test_io_open_and_write()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    __d_println(DEBUG, "opening %p\n");
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    __d_println(DEBUG, "writing %p\n");
    ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, rw_context(handle, data, MAX_READ)));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(MAX_READ, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(written);
    TEST_ASSERT_EQUAL(&data, write_data);
}

void test_console_write_outputs_to_basic_io()
{
    IOHandle handle = -1;
    char data[] = "hello console";
    BasicIO__reset_output();

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, console_path, Console__heap_new(), (IOStatePtr) ConsoleState__instance()))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), console_path, IO_OPEN_WRITE))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, rw_context(handle, data, sizeof(data) - 1)));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(sizeof(data) - 1, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL_STRING(data, BasicIO__output());
}

void test_io_open_and_read_when_no_data_ready_parks_the_process()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr read_context = rw_context(handle, data, MAX_READ);

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, read_context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_TRUE(opened);
    TEST_ASSERT_TRUE(read);

    TEST_ASSERT_NOT_NULL(io_message);
    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, io_message->source_code);
    Payload payload = UnwrapPanic(Payload, io_message->payload);

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_DEFERRED, payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, payload.deferred.anything);
    TEST_ASSERT_EQUAL(read_context, payload.deferred.context);
}

void test_io_open_and_read_when_no_data_ready_parks_the_process_and_unparks_when_data_arrives()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr read_context = rw_context(handle, data, MAX_READ);
    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, read_context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    MessagePtr park_message = io_message;

    TEST_ASSERT_NOT_NULL(park_message);
    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, park_message->source_code);
    Payload park_payload = UnwrapPanic(Payload, park_message->payload);

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_DEFERRED, park_payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, park_payload.deferred.anything);


    data_available_interrupt(io_subsystem);

    TEST_ASSERT_NOT_NULL(park_payload.deferred.continuation);
    TEST_ASSERT_EQUAL(read_context, park_payload.deferred.context);

    MessagePtr available_message = io_message;

    TEST_ASSERT_NOT_EQUAL(park_message, available_message);
    TEST_ASSERT_EQUAL(SUBSYS_IO_AVAILABLE, available_message->source_code);
    Payload available_payload = UnwrapPanic(Payload, available_message->payload);

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_ANYTHING, available_payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, available_payload.anything);
}

void test_io_write_when_not_open()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, rw_context(handle, data, MAX_READ)));
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

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, rw_context(handle, data, MAX_READ)));
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
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&io_subsystem->process_handles, PROC.pid));
        IsNone(TEST_FAIL());
        IsSome(handles,
        {
            Match(ArrayIndex, PIDHandles__find_item(handles, EXPECTED_OPEN_HANDLE))
                IsSome(_, TEST_FAIL())
            End
        })
    End


    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_close_fails_on_second_time()
{
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, close_context(EXPECTED_OPEN_HANDLE)))
        IsSuccess(result, TEST_FAIL());
    End

    TEST_ASSERT_TRUE(closed);
}

void test_io_subsystem_receiving_process_termination_closes_handles_for_that_process()
{
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, PROC_STATE_DEAD, PID);
    MessageBroker__deliver(kernel->message_broker, message);

    TEST_ASSERT_TRUE(closed);
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, closed_handle);

    MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&io_subsystem->process_handles, PID))
        IsSome(_, TEST_FAIL());
    End
}

void test_io_subsystem_receiving_other_process_messages_does_nothing()
{
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL())
    End

    MessagePtr message = Message__heap_new_anything(SOURCE_PROCESS, PROC_STATE_WAITING, PID);
    MessageBroker__deliver(kernel->message_broker, message);

    TEST_ASSERT_FALSE(closed);
    TEST_ASSERT_NOT_EQUAL(INTERNAL_HANDLE, closed_handle);

    MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&io_subsystem->process_handles, PID))
        IsSome(handles,
        {
            TEST_ASSERT_EQUAL(1, PIDHandles__len(handles));
        })
    End

}

#include "unity.h"
#include "../../stubs.h"
#include "../../stubs/process.h"
#include "../../fixtures/kernel.h"
#include "../../../../kernel/subsystems/message/io_operation_message.h"
#include "../../../../kernel/subsystems/message/subsystem_codes.h"
#include "../../../../kernel/subsystems/io/console/console.h"
#include "../../../../kernel/subsystems/io/register.h"
#include "../../../../kernel/subsystems/io/stream/stream.h"
#include "../../../../kernel/scheduler/context_switch.h"
#include "../../../../kernel/scheduler/register.h"
#include "../../../../kernel/arch/process.h"
#include "../../../../kernel/arch/page_table.h"

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
static Process SERVER_PROC = STUB_PROCESS(2);
static Optional(ProcessPtr) proc = Some(ProcessPtr, &PROC);
static Optional(ProcessID) pid = Some(ProcessID, PID);

static Optional(Message) io_message = None(Message);

static Flags FLAGS = 1234;

static Char expected[] = {1, 2, 3, 4};

static BString path = BSTRING_NEW(":test:");
static BString second_path = BSTRING_NEW(":test2:");
static BString console_path = BSTRING_NEW(":console:");
static BString tty_path = BSTRING_NEW(":tty:");
static BString hello_stream_path = BSTRING_NEW("/dev/hello");
static BString invalid_path = BSTRING_NEW("/sss");

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
static FakeIOState SECOND_STATE = NEW_FAKE_IO_STATE;

void reset_state()
{
    opened = false;
    closed = false;
    written = false;
    read = false;

    closed_handle = -1;

    write_data = NULL;
    PROC = STUB_PROCESS(PID);
    SERVER_PROC = STUB_PROCESS(2);
    SERVER_PROC.process_class = PROCESS_CLASS_SERVICE;
    SERVER_PROC.service = ServiceState__new();
    SERVER_PROC.user_allocations = UserAllocations__new();
    SERVER_PROC.allocation_next = USER_ALLOC_BASE;
    SERVER_PROC.page_table = PageTable__new(kernel);
    STATE = NEW_FAKE_IO_STATE;
    SECOND_STATE = NEW_FAKE_IO_STATE;
    buffer = IOBuffer__heap_new();
    clear_deferred_syscall_result();
}

static Result(IOInternalHandle) fake_open(IOStatePtr state_ptr, BString path, Flags flags)
{
    TEST_ASSERT_EQUAL(FLAGS, flags);

    IOHandleArray__push(&state_ptr->handles, INTERNAL_HANDLE);
    opened = true;
    return Success(IOInternalHandle, INTERNAL_HANDLE);
}

static Result(IOBufferPtr) fake_read(
    IOStatePtr state_ptr, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    read = true;
    if (IOBuffer__item_count(buffer) == 0)
    {
        return Error(IOBufferPtr, bstring("no data"), IO_WAIT_FOR_DATA);
    }
    return Success(IOBufferPtr, buffer);
}

static Result(ByteCount) fake_write(IOStatePtr state_ptr, IOInternalHandle handle, DataPtr data, size_t size)
{
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);
    TEST_ASSERT_EQUAL(MAX_READ, size);

    written = true;
    write_data = data;
    return Success(ByteCount, size);
}

static Result(IOInternalHandle) fake_close(IOStatePtr state_ptr, IOInternalHandle handle)
{
    TEST_ASSERT_EQUAL(INTERNAL_HANDLE, handle);

    closed = true;
    closed_handle = handle;

    if (!opened)
    {
        return Error(IOInternalHandle, bstring("not opened"), IO_NOT_OPEN);
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
    io_message = Some(Message, *message);
}

void setUp(void)
{
    kernel = init_test_kernel();
    io_subsystem = kernel->io_subsystem;
    reset_state();

    io_message = None(Message);

    Scheduler__schedule(kernel->scheduler, &PROC);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    Listener listener = KernelListener__new(kernel, SOURCE_IO, io_available_callback);
    MessageBroker__register_listener(io_subsystem->message_broker, listener);

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, path, &TEST_IO, &STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, second_path, &TEST_IO, &SECOND_STATE))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ListenerRegisterResult, IOSubsystem__register_listeners(kernel))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(SchedulerRegisterCode, Scheduler__register(kernel))
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

void data_available_interrupt_for_state(IOSubsystemPtr io_sub, FakeIOStatePtr state)
{
    Match(IOContextPtr, state->parked_context)
        IsSome(parked_context,
        {
            IOBuffer__push(buffer, 1);
            IOBuffer__push(buffer, 2);
            IOBuffer__push(buffer, 3);
            IOBuffer__push(buffer, 4);
            ResultMatch(IOStatus, IOSubsystem__data_arrived(io_sub, parked_context))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsNone(
        {
            TEST_FAIL();
        })
    End
}

void data_available_interrupt(IOSubsystemPtr io_sub)
{
    data_available_interrupt_for_state(io_sub, &STATE);
}

IOServerBufferDataPtr server_buffer_data_for_test(ProcessPtr process, IOServerBuffer buffer)
{
    ResultMatch(Address, Process__device_address(process, (Address) buffer.data))
        IsSuccess(address, return (IOServerBufferDataPtr) address);
        IsError(code, message, TEST_FAIL())
    End

    TEST_FAIL();
    return NULL;
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

void test_io_open_records_external_handle_when_process_already_has_handles()
{
    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, TEST_ASSERT_EQUAL_INT(EXPECTED_OPEN_HANDLE, result));
        IsError(code, message, TEST_FAIL());
    End

    MapPtrMatch(ProcessHandleMap, ProcessHandleMap__get_ptr(&io_subsystem->process_handles, PID));
        IsNone(TEST_FAIL());
        IsSome(handles,
        {
            TEST_ASSERT_EQUAL_INT(2, PIDHandles__len(handles));
            TEST_ASSERT_EQUAL(EXPECTED_OPEN_HANDLE, handles->data[0]);
            TEST_ASSERT_EQUAL(EXPECTED_OPEN_HANDLE, handles->data[1]);
            TEST_ASSERT_NOT_EQUAL(INTERNAL_HANDLE, handles->data[1]);
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

    BasicIO__reset_output();

    ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, rw_context(handle, data, sizeof(data) - 1)));
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(sizeof(data) - 1, byte_count);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL_STRING(data, BasicIO__output());
}

void test_mkdir_and_rm_on_console_return_unsupported()
{
    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, console_path, Console__heap_new(), (IOStatePtr) ConsoleState__instance()))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOStatus, IOSubsystem__mkdir(io_subsystem, console_path))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End

    ResultMatch(IOStatus, IOSubsystem__remove(io_subsystem, console_path, 0))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_stream_client_write_buffers_data_for_write_server_until_consumed()
{
    IOHandle client_handle = -1;
    IOHandle server_handle = -1;
    char data[] = "hello stream";
    StreamStatePtr stream_state = StreamState__heap_new();

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, tty_path, Stream__heap_new(), (IOStatePtr) stream_state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__server_register(io_subsystem, open_context(), &SERVER_PROC, tty_path, IO_SERVER_WRITE))
        IsSuccess(result, server_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), tty_path, IO_OPEN_WRITE))
        IsSuccess(result, client_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__write(io_subsystem, rw_context(client_handle, (DataPtr) data, sizeof(data) - 1)));
        IsSuccess(byte_count, TEST_ASSERT_EQUAL_UINT(sizeof(data) - 1, byte_count));
        IsError(code, message, TEST_FAIL());
    End

    Match(ServiceEvent, ServiceEvents__pop_front(&SERVER_PROC.service.events))
        IsSome(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IO, event.type);
            TEST_ASSERT_EQUAL(SERVICE_IO_WRITE_REQUEST, event.source);
        })
        IsNone(TEST_FAIL())
    End

    IOServerBuffer server_buffer;
    ResultMatch(IOStatus, IOSubsystem__server_get_buffer(io_subsystem, close_context(server_handle), &SERVER_PROC, &server_buffer))
        IsSuccess(status,
        {
            TEST_ASSERT_EQUAL_INT(IO_OK, status);
            TEST_ASSERT_EQUAL_UINT(sizeof(data) - 1, server_buffer.capacity);
        });
        IsError(code, message, TEST_FAIL());
    End

    for (size_t i = 0; i < sizeof(data) - 1; i++)
    {
        TEST_ASSERT_EQUAL_UINT8(data[i], stream_state->write_buffer.buffer[i]);
    }

    ResultMatch(IOStatus, IOSubsystem__server_data_consumed(io_subsystem, close_context(server_handle), sizeof(data) - 1))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status));
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_EQUAL_UINT(0, IOBuffer__item_count(&stream_state->write_buffer));
}

void test_mkdir_and_rm_on_stream_return_unsupported()
{
    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, tty_path, Stream__heap_new(), (IOStatePtr) StreamState__heap_new()))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOStatus, IOSubsystem__mkdir(io_subsystem, tty_path))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End

    ResultMatch(IOStatus, IOSubsystem__remove(io_subsystem, tty_path, 0))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(IO_OPERATION_NOT_SUPPORTED, code))
    End
}

void test_stream_read_when_no_data_ready_parks_the_process()
{
    IOHandle handle = -1;
    Data data[MAX_READ] = {0};

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, tty_path, Stream__heap_new(), (IOStatePtr) StreamState__heap_new()))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), tty_path, IO_OPEN_READ))
        IsSuccess(result, handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr read_context = rw_context(handle, data, MAX_READ);

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, read_context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_TRUE(io_message.present);
    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, io_message.some.val.source_code);
    Payload payload = Unwrap(Payload, io_message.some.val.payload, (Payload){});

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_DEFERRED, payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, payload.deferred.anything);
    TEST_ASSERT_TRUE(payload.deferred.owns_context);
    TEST_ASSERT_NOT_EQUAL((uintptr_t) read_context, (uintptr_t) payload.deferred.context);

    IOContextPtr parked_context = (IOContextPtr) payload.deferred.context;
    ExternalBuffer parked_buffer = Unwrap(ExternalBuffer, parked_context->buffer, (ExternalBuffer){});
    TEST_ASSERT_EQUAL(handle, Unwrap(IOHandle, parked_context->handle, -1));
    TEST_ASSERT_EQUAL_PTR(data, parked_buffer.data);
}

void test_stream_read_resumes_through_context_switch_when_data_arrives()
{
    IOHandle read_handle = -1;
    IOHandle server_handle = -1;
    char data[] = "hello stream";
    Data read_data[MAX_READ] = {0};
    StreamStatePtr stream_state = StreamState__heap_new();

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, tty_path, Stream__heap_new(), (IOStatePtr) stream_state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__server_register(io_subsystem, open_context(), &SERVER_PROC, tty_path, IO_SERVER_READ))
        IsSuccess(result, server_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), tty_path, IO_OPEN_READ))
        IsSuccess(result, read_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr read_context = rw_context(read_handle, read_data, MAX_READ);

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, read_context))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code))
    End

    IOServerBuffer server_buffer;
    ResultMatch(IOStatus, IOSubsystem__server_get_buffer(io_subsystem, close_context(server_handle), &SERVER_PROC, &server_buffer))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    for (size_t i = 0; i < sizeof(data) - 1; i++)
    {
        server_buffer_data_for_test(&SERVER_PROC, server_buffer)[i] = data[i];
    }

    ResultMatch(IOStatus, IOSubsystem__server_data_available(io_subsystem, close_context(server_handle), sizeof(data) - 1))
        IsSuccess(status, TEST_ASSERT_EQUAL_INT(IO_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    ContextSwitch__switch(ContextSwitch__new(&PROC, None(ProcessPtr)));

    ResultMatch(Anything, deferred_syscall_result())
        IsSuccess(byte_count,
        {
            TEST_ASSERT_EQUAL_UINT(sizeof(data) - 1, byte_count);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(data, read_data, byte_count);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_stream_unpark_and_close_update_parked_contexts_under_lock()
{
    IOHandle first_handle = -1;
    IOHandle second_handle = -1;
    Data first_data[MAX_READ] = {0};
    Data second_data[MAX_READ] = {0};
    StreamStatePtr stream_state = StreamState__heap_new();
    IOPtr stream = Stream__heap_new();

    ResultMatch(DeviceHandle, IOSubsystem__register_device(io_subsystem, tty_path, stream, (IOStatePtr) stream_state))
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), tty_path, IO_OPEN_READ))
        IsSuccess(result, first_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), tty_path, IO_OPEN_READ))
        IsSuccess(result, second_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr first_context = rw_context(first_handle, first_data, MAX_READ);
    IOContextPtr second_context = rw_context(second_handle, second_data, MAX_READ);

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, first_context))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code))
    End

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, second_context))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code))
    End

    TEST_ASSERT_EQUAL_UINT64(2, StreamParkedContexts__len(&stream_state->parked_contexts));

    Match(IOContextPtr, stream->data_available((IOStatePtr) stream_state, 1))
        IsSome(parked_context, TEST_ASSERT_EQUAL(second_handle, Unwrap(IOHandle, parked_context->handle, -1)))
        IsNone(TEST_FAIL())
    End

    ResultMatch(ParkStatus, stream->unpark((IOStatePtr) stream_state))
        IsSuccess(status, TEST_ASSERT_EQUAL(IO_UNPARK_OK, status))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL_UINT64(1, StreamParkedContexts__len(&stream_state->parked_contexts));
    Match(IOContextPtr, StreamParkedContexts__at(&stream_state->parked_contexts, -1))
        IsSome(parked_context, TEST_ASSERT_EQUAL(first_handle, Unwrap(IOHandle, parked_context->handle, -1)))
        IsNone(TEST_FAIL())
    End

    ResultMatch(IOHandle, IOSubsystem__close(io_subsystem, close_context(first_handle)))
        IsSuccess(closed_handle, TEST_ASSERT_EQUAL(first_handle, closed_handle))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL_UINT64(0, StreamParkedContexts__len(&stream_state->parked_contexts));
}

void test_hello_stream_read_returns_server_written_data_after_data_available()
{
    IOHandle read_handle = -1;
    IOHandle write_handle = -1;
    Data data[MAX_READ] = {0};

    kernel = init_test_kernel_without_filesystem();
    io_subsystem = kernel->io_subsystem;
    io_message = None(Message);
    ResultMatch(SchedulerRegisterCode, Scheduler__register(kernel))
        IsError(code, message, TEST_FAIL());
    End
    Scheduler__schedule(kernel->scheduler, &PROC);
    Scheduler__preempt(kernel->scheduler, cpu, context);

    ResultMatch(RegisterCode, IOSubsystem__register(kernel))
        IsError(code, message, TEST_FAIL());
    End

    Listener listener = KernelListener__new(kernel, SOURCE_IO, io_available_callback);
    MessageBroker__register_listener(io_subsystem->message_broker, listener);

    KernelContext server_kernel_context = KernelContext__new(
        Some(ProcessID, SERVER_PROC.pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr server_open_context = IOContext__heap_new(
        server_kernel_context, Some(IOSubsystemPtr, io_subsystem), None(IOHandle), None(ExternalBuffer)
    );

    ResultMatch(IOHandle, IOSubsystem__server_register(io_subsystem, server_open_context, &SERVER_PROC, hello_stream_path, IO_SERVER_READ))
        IsSuccess(result, write_handle = result);
        IsError(code, message, TEST_FAIL());
    End
    IOContext__dispose(server_open_context);

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), hello_stream_path, IO_OPEN_READ))
        IsSuccess(result, read_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    IOContextPtr read_context = rw_context(read_handle, data, MAX_READ);

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, read_context));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    TEST_ASSERT_TRUE(io_message.present);
    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, io_message.some.val.source_code);

    Match(ServiceEvent, ServiceEvents__pop_front(&SERVER_PROC.service.events))
        IsSome(event,
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IO, event.type);
            TEST_ASSERT_EQUAL(SERVICE_IO_READ_REQUEST, event.source);
        })
        IsNone(TEST_FAIL())
    End

    IOServerBuffer server_buffer;
    Address allocation_start = SERVER_PROC.allocation_next;
    ResultMatch(IOStatus, IOSubsystem__server_get_buffer(io_subsystem, close_context(write_handle), &SERVER_PROC, &server_buffer))
        IsSuccess(status,
        {
            TEST_ASSERT_EQUAL_INT(IO_OK, status);
            TEST_ASSERT_TRUE(server_buffer.capacity > 0);
        });
        IsError(code, message, TEST_FAIL());
    End

    TEST_ASSERT_TRUE((Address) server_buffer.data >= allocation_start);
    TEST_ASSERT_TRUE((Address) server_buffer.data < allocation_start + PAGE_SIZE);
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

    TEST_ASSERT_TRUE(io_message.present);
    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, io_message.some.val.source_code);
    Payload payload = Unwrap(Payload, io_message.some.val.payload, (Payload){});

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_DEFERRED, payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, payload.deferred.anything);
    TEST_ASSERT_TRUE(payload.deferred.owns_context);
    TEST_ASSERT_NOT_EQUAL((uintptr_t) read_context, (uintptr_t) payload.deferred.context);

    IOContextPtr parked_context = (IOContextPtr) payload.deferred.context;
    ExternalBuffer parked_buffer = Unwrap(ExternalBuffer, parked_context->buffer, (ExternalBuffer){});
    TEST_ASSERT_EQUAL(handle, Unwrap(IOHandle, parked_context->handle, -1));
    TEST_ASSERT_EQUAL_PTR(data, parked_buffer.data);
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

    Message park_message = io_message.some.val;

    TEST_ASSERT_EQUAL(SUBSYS_IO_PARK, park_message.source_code);
    Payload park_payload = Unwrap(Payload, park_message.payload, (Payload){});

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_DEFERRED, park_payload.type);
    TEST_ASSERT_EQUAL(PROC.pid, park_payload.deferred.anything);
    TEST_ASSERT_TRUE(park_payload.deferred.owns_context);
    TEST_ASSERT_TRUE(park_payload.deferred.operation_key.present);
    TEST_ASSERT_EQUAL_UINT64(handle, park_payload.deferred.operation_key.some.val);


    data_available_interrupt(io_subsystem);

    TEST_ASSERT_NOT_NULL(park_payload.deferred.continuation);
    TEST_ASSERT_NOT_EQUAL((uintptr_t) read_context, (uintptr_t) park_payload.deferred.context);

    IOContextPtr parked_context = (IOContextPtr) park_payload.deferred.context;
    ExternalBuffer parked_buffer = Unwrap(ExternalBuffer, parked_context->buffer, (ExternalBuffer){});
    TEST_ASSERT_EQUAL(handle, Unwrap(IOHandle, parked_context->handle, -1));
    TEST_ASSERT_EQUAL_PTR(data, parked_buffer.data);

    Message available_message = io_message.some.val;

    TEST_ASSERT_NOT_EQUAL(park_message.source_code, available_message.source_code);
    TEST_ASSERT_EQUAL(SUBSYS_IO_OPERATION_AVAILABLE, available_message.source_code);
    Payload available_payload = Unwrap(Payload, available_message.payload, (Payload){});

    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_IO_OPERATION, available_payload.type);
    TEST_ASSERT_EQUAL(PID, available_payload.io_operation.pid);
    TEST_ASSERT_EQUAL_UINT64(handle, available_payload.io_operation.operation_key);
}

void test_io_open_and_read_when_two_handles_are_parked_unpark_signal_targets_the_matching_handle(void)
{
    IOHandle first_handle = -1;
    IOHandle second_handle = -1;
    Data first_data[MAX_READ] = {0};
    Data second_data[MAX_READ] = {0};

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), path, FLAGS))
        IsSuccess(result, first_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(IOHandle, IOSubsystem__open(io_subsystem, open_context(), second_path, FLAGS))
        IsSuccess(result, second_handle = result);
        IsError(code, message, TEST_FAIL());
    End

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, rw_context(first_handle, first_data, MAX_READ)));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    ResultMatch(ByteCount, IOSubsystem__read(io_subsystem, rw_context(second_handle, second_data, MAX_READ)));
        IsSuccess(count, TEST_FAIL());
        IsError(code, message, TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code));
    End

    data_available_interrupt_for_state(io_subsystem, &SECOND_STATE);

    TEST_ASSERT_TRUE(io_message.present);
    TEST_ASSERT_EQUAL(SUBSYS_IO_OPERATION_AVAILABLE, io_message.some.val.source_code);
    Payload second_payload = Unwrap(Payload, io_message.some.val.payload, (Payload){});
    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_IO_OPERATION, second_payload.type);
    TEST_ASSERT_EQUAL(PID, second_payload.io_operation.pid);
    TEST_ASSERT_EQUAL_UINT64(second_handle, second_payload.io_operation.operation_key);

    data_available_interrupt_for_state(io_subsystem, &STATE);

    TEST_ASSERT_TRUE(io_message.present);
    TEST_ASSERT_EQUAL(SUBSYS_IO_OPERATION_AVAILABLE, io_message.some.val.source_code);
    Payload first_payload = Unwrap(Payload, io_message.some.val.payload, (Payload){});
    TEST_ASSERT_EQUAL(PAYLOAD_TYPE_IO_OPERATION, first_payload.type);
    TEST_ASSERT_EQUAL(PID, first_payload.io_operation.pid);
    TEST_ASSERT_EQUAL_UINT64(first_handle, first_payload.io_operation.operation_key);
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

    MessagePtr message = Message__heap_new_process_id(SOURCE_PROCESS, PROC_STATE_DEAD, PID);
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

    MessagePtr message = Message__heap_new_process_id(SOURCE_PROCESS, PROC_STATE_WAITING, PID);
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

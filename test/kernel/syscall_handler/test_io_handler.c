#include "unity.h"

#include "../../../kernel/syscall/io_handler.h"
#include "../../../kernel/syscall/syslog_handler.h"
#include "../../../kernel/subsystems/io/io_subsystem.h"
#include "../../../common_includes/syscalls/io.h"
#include "../../../common_includes/syscalls/syslog.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs.h"
#include "../stubs/panic.h"

static const ProcessID PID = 44;
static const IOInternalHandle INTERNAL_HANDLE = 0x42;
static const IOHandle RAW_HANDLE = 0x0000000100000042;
static const FileDescriptor EXPECTED_FD = 3;
static const Flags FLAGS = IO_OPEN_WRITE;
static BString path = BSTRING_NEW(":test:");

static KernelPtr kernel = NULL;
static Process process = STUB_PROCESS(PID);
static IOState state = NEW_IO_STATE;
static bool opened = false;
static bool written = false;
static bool closed = false;
static bool data_available = false;
static bool eof = false;
static IOInternalHandle write_handle = 0;
static DataPtr write_data = NULL;
static size_t write_size = 0;
static char server_buffer_data[16] = { 0 };

static Result(IOInternalHandle) fake_open(IOStatePtr state_ptr, BString open_path, Flags flags)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    TEST_ASSERT_TRUE(BString__equals(&path, &open_path));
    TEST_ASSERT_EQUAL(FLAGS, flags);
    opened = true;
    return Success(IOInternalHandle, INTERNAL_HANDLE);
}

static Result(ByteCount) fake_write(IOStatePtr state_ptr, IOInternalHandle handle, DataPtr data, size_t size)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    written = true;
    write_handle = handle;
    write_data = data;
    write_size = size;
    return Success(ByteCount, size);
}

static Result(IOInternalHandle) fake_close(IOStatePtr state_ptr, IOInternalHandle handle)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    closed = true;
    return Success(IOInternalHandle, handle);
}

static Result(IOBufferPtr) fake_read(
    IOStatePtr state_ptr, IOInternalHandle handle, size_t size, ExternalBuffer target_buffer
)
{
    return Error(IOBufferPtr, bstring("not implemented"), IO_OPERATION_NOT_IMPLEMENTED);
}

static Result(ParkStatus) fake_park(IOStatePtr state_ptr, IOContextPtr parked_context)
{
    return Success(ParkStatus, IO_PARK_OK);
}

static Result(ParkStatus) fake_unpark(IOStatePtr state_ptr)
{
    return Success(ParkStatus, IO_UNPARK_OK);
}

static Optional(IOContextPtr) fake_data_available(IOStatePtr state_ptr, size_t byte_count)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    TEST_ASSERT_EQUAL(7, byte_count);
    data_available = true;
    return None(IOContextPtr);
}

static Optional(IOContextPtr) fake_eof(IOStatePtr state_ptr)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    eof = true;
    return None(IOContextPtr);
}

static Result(IOServerBuffer) fake_server_buffer(IOStatePtr state_ptr)
{
    TEST_ASSERT_EQUAL(&state, state_ptr);
    return Success(IOServerBuffer, ((IOServerBuffer) {
        .data = server_buffer_data,
        .capacity = sizeof(server_buffer_data),
    }));
}

static IO test_io = {
    .open = fake_open,
    .read = fake_read,
    .write = fake_write,
    .close = fake_close,
    .park = fake_park,
    .unpark = fake_unpark,
    .server_buffer = fake_server_buffer,
    .data_available = fake_data_available,
    .eof = fake_eof,
};

static void install_open_fd(FileDescriptor fd)
{
    KernelContext kernel_context = KernelContext__new(
        Some(ProcessID, process.pid), Some(SchedulerPtr, kernel->scheduler)
    );
    IOContextPtr context = IOContext__heap_new(
        kernel_context, Some(IOSubsystemPtr, kernel->io_subsystem), None(IOHandle), None(ExternalBuffer)
    );

    ResultMatch(IOHandle, IOSubsystem__open(kernel->io_subsystem, context, path, FLAGS))
        IsSuccess(handle,
        {
            TEST_ASSERT_EQUAL(RAW_HANDLE, handle);
            Process__set_file_descriptor(&process, fd, handle);
        })
        IsError(code, message, TEST_FAIL())
    End

    IOContext__dispose(context);
    opened = false;
}

void setUp(void)
{
    clear_panic_state();
    kernel = init_test_kernel_without_filesystem();
    process = STUB_PROCESS(PID);
    state = NEW_IO_STATE;
    opened = false;
    written = false;
    closed = false;
    data_available = false;
    eof = false;
    write_handle = 0;
    write_data = NULL;
    write_size = 0;

    ResultMatch(DeviceHandle, IOSubsystem__register_device(kernel->io_subsystem, path, &test_io, &state))
        IsError(code, message, TEST_FAIL())
    End
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_io_handler_open_returns_process_file_descriptor(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_OPEN, (Anything) cstr(path), FLAGS, 0, 0
    ))
        IsSuccess(fd,
        {
            TEST_ASSERT_TRUE(opened);
            TEST_ASSERT_EQUAL(EXPECTED_FD, fd);
            TEST_ASSERT_EQUAL(1, process.file_descriptors.len);

            ResultMatch(ProcessIOHandle, Process__get_io_handle(&process, EXPECTED_FD))
                IsSuccess(handle, TEST_ASSERT_EQUAL(RAW_HANDLE, handle))
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_handler_write_translates_file_descriptor_to_io_handle(void)
{
    char data[] = "abc";
    install_open_fd(EXPECTED_FD);

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_WRITE, EXPECTED_FD, (Anything) data, sizeof(data) - 1, 0
    ))
        IsSuccess(bytes,
        {
            TEST_ASSERT_TRUE(written);
            TEST_ASSERT_EQUAL(sizeof(data) - 1, bytes);
            TEST_ASSERT_EQUAL(INTERNAL_HANDLE, write_handle);
            TEST_ASSERT_EQUAL(data, write_data);
            TEST_ASSERT_EQUAL(sizeof(data) - 1, write_size);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_handler_open_rejects_missing_path_at_syscall_boundary(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_OPEN, 0, FLAGS, 0, 0
    ))
        IsSuccess(fd, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End
}

void test_io_handler_close_removes_file_descriptor(void)
{
    install_open_fd(EXPECTED_FD);

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_CLOSE, EXPECTED_FD, 0, 0, 0
    ))
        IsSuccess(fd,
        {
            TEST_ASSERT_TRUE(closed);
            TEST_ASSERT_EQUAL(EXPECTED_FD, fd);
        })
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ProcessIOHandle, Process__get_io_handle(&process, EXPECTED_FD))
        IsSuccess(handle, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_NOT_OPEN, code))
    End
}

void test_io_handler_list_dir_rejects_missing_path_at_syscall_boundary(void)
{
    DirectoryEntry entries[2] = { 0 };

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_LIST_DIR, 0, (Anything) entries, 2, 0
    ))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End
}

void test_io_handler_list_dir_rejects_missing_entries_at_syscall_boundary(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_LIST_DIR, (Anything) cstr(path), 0, 2, 0
    ))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End
}

void test_io_handler_mkdir_rejects_missing_path_at_syscall_boundary(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_MKDIR, 0, 0, 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End
}

void test_io_handler_mount_rejects_missing_arguments_at_syscall_boundary(void)
{
    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_MOUNT, 0, (Anything) "ext2", (Anything) "/", 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_MOUNT, (Anything) "/dev/virtio1", 0, (Anything) "/", 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_MOUNT, (Anything) "/dev/virtio1", (Anything) "ext2", 0, 0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_INVALID_PATH, code))
    End
}

void test_io_handler_mount_requires_mount_capability(void)
{
    Process__set_user_identity(&process);

    ResultMatch(SystemCallResult, IO__handler(
        kernel,
        Some(ProcessPtr, &process),
        IO_MOUNT,
        (Anything) "/dev/virtio1",
        (Anything) "ext2",
        (Anything) "/",
        0
    ))
        IsSuccess(status, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, code))
    End
}

void test_io_handler_server_data_available_translates_file_descriptor_to_io_handle(void)
{
    install_open_fd(EXPECTED_FD);

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_SERVER_DATA_AVAILABLE, EXPECTED_FD, 7, 0, 0
    ))
        IsSuccess(status,
        {
            TEST_ASSERT_TRUE(data_available);
            TEST_ASSERT_EQUAL(IO_OK, status);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_handler_server_register_fd_can_be_used_for_get_buffer(void)
{
    ProcessPtr service_process = dummy_process(100, 1, PROC_STATE_RUNNING);
    IOServerBuffer server_buffer = { 0 };

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, service_process), IO_SERVER_REGISTER, (Anything) cstr(path), IO_SERVER_READ, 0, 0
    ))
        IsSuccess(fd,
        {
            TEST_ASSERT_EQUAL(EXPECTED_FD, fd);

            ResultMatch(ProcessIOHandle, Process__get_io_handle(service_process, (FileDescriptor) fd))
                IsSuccess(handle, TEST_ASSERT_EQUAL(RAW_HANDLE, handle))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(SystemCallResult, Syslog__handler(
                kernel,
                Some(ProcessPtr, service_process),
                (Anything) "uart_rx: wait_status=0 event_type=3 event_source=1 event_data=2\n",
                (Anything) "UART-RX",
                0,
                0,
                0
            ))
                IsSuccess(status, TEST_ASSERT_EQUAL(SYSLOG_OK, status))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(SystemCallResult, Syslog__handler(
                kernel,
                Some(ProcessPtr, service_process),
                (Anything) "uart_rx: uart_irq=0 read_request=1\n",
                (Anything) "UART-RX",
                0,
                0,
                0
            ))
                IsSuccess(status, TEST_ASSERT_EQUAL(SYSLOG_OK, status))
                IsError(code, message, TEST_FAIL())
            End

            ResultMatch(SystemCallResult, IO__handler(
                kernel,
                Some(ProcessPtr, service_process),
                IO_SERVER_GET_BUFFER,
                fd,
                (Anything) &server_buffer,
                0,
                0
            ))
                IsSuccess(status,
                {
                    TEST_ASSERT_EQUAL(IO_OK, status);
                    TEST_ASSERT_EQUAL_UINT64(sizeof(server_buffer_data), server_buffer.capacity);
                    TEST_ASSERT_NOT_NULL(server_buffer.data);
                })
                IsError(code, message, TEST_FAIL())
            End
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_io_handler_server_eof_translates_file_descriptor_to_io_handle(void)
{
    install_open_fd(EXPECTED_FD);

    ResultMatch(SystemCallResult, IO__handler(
        kernel, Some(ProcessPtr, &process), IO_SERVER_EOF, EXPECTED_FD, 0, 0, 0
    ))
        IsSuccess(status,
        {
            TEST_ASSERT_TRUE(eof);
            TEST_ASSERT_EQUAL(IO_OK, status);
        })
        IsError(code, message, TEST_FAIL())
    End
}

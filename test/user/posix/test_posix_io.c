#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

#include "unity.h"

#include "../../../common/ipc/io_protocol.h"
#include "../../../user/lib/io/client.h"
#include "../../../user/lib/sys/io.h"
#include "../../../user/lib/trace/io_trace.h"

static IOHandle opened_handle;
static BString opened_path;
static Flags opened_flags;
static IOHandle closed_handle;
static ByteCount next_read_result;
static ByteCount next_write_result;
static IOPipe next_pipe;
static Flags last_read_flags;
static Flags last_write_flags;
static int terminal_descriptor;
static BString terminal_path;
static unsigned inheritance_prepares;
static unsigned inheritance_commits;
static unsigned inheritance_aborts;
static unsigned inheritance_claims;
static unsigned trace_resets;
static uint64_t inheritance_commit_pid;
static uint64_t current_pid;
static unsigned remove_calls;

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    opened_path = *path;
    opened_flags = flags;
    return opened_handle < 0 ? Error(IOHandle, bstr("open failed"), opened_handle) : Success(IOHandle, opened_handle);
}

int PosixPath__absolute(const char * path, BStringPtr result)
{
    BString argument = BString__new(path);
    if (BString__starts_with(&argument, &bstr("/")))
    {
        *result = argument;
        return 0;
    }
    *result = BString__append(&bstr("/"), &argument);
    return 0;
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buffer, size_t size)
{
    if (next_read_result > 0)
    {
        memcpy(buffer, "data", (size_t)next_read_result);
    }
    return next_read_result < 0 ? Error(ByteCount, bstr("read failed"), next_read_result)
                                : Success(ByteCount, next_read_result);
}

Result(ByteCount) IO__read_bytes_with_flags(IOHandle handle, char * buffer, size_t size, Flags flags)
{
    last_read_flags = flags;
    return IO__read_bytes(handle, buffer, size);
}

Result(ByteCount) IO__write_bytes(IOHandle handle, const char * buffer, size_t size)
{
    return next_write_result < 0 ? Error(ByteCount, bstr("write failed"), next_write_result)
                                 : Success(ByteCount, next_write_result);
}

Result(ByteCount) IO__write_bytes_with_flags(IOHandle handle, const char * buffer, size_t size, Flags flags)
{
    last_write_flags = flags;
    return IO__write_bytes(handle, buffer, size);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    closed_handle = handle;
    return Success(IOStatus, IO_OK);
}

Result(IOPipe) IO__pipe(void)
{
    return next_pipe.read < 0 ? Error(IOPipe, bstr("pipe failed"), next_pipe.read) : Success(IOPipe, next_pipe);
}

Result(IOStatus) IO__remove_file(ConstBStringPtr path)
{
    remove_calls++;
    return Error(IOStatus, bstr("invalid path"), IO_INVALID_PATH);
}

Optional(BString) IO__terminal_path(int descriptor)
{
    return descriptor == terminal_descriptor ? Some(BString, terminal_path) : None(BString);
}

Result(IOClientInheritance) IOClient__prepare_inheritance(IOHandle handle)
{
    inheritance_prepares++;
    return Success(IOClientInheritance, ((IOClientInheritance){.local = handle, .token = (uint64_t)handle}));
}

IOStatus IOClient__commit_inheritance(IOClientInheritance * inheritance, uint64_t child_pid)
{
    inheritance_commits++;
    inheritance_commit_pid = child_pid;
    return IO_OK;
}

IOStatus IOClient__abort_inheritance(IOClientInheritance * inheritance)
{
    inheritance_aborts++;
    return IO_OK;
}

void IOClient__reset_after_fork(void)
{
}

IOStatus IOClient__claim_inheritance(IOClientInheritance * inheritance)
{
    inheritance_claims++;
    return IO_OK;
}

void IO__reset_after_fork(void)
{
}

void IOTrace__reset_after_fork(void)
{
    trace_resets++;
}

uint64_t process_current_pid(void)
{
    return current_pid;
}

#include "../../../user/lib/newlib/exec_payload.c"
#include "../../../user/lib/newlib/posix_io.c"

void setUp(void)
{
    remove_calls = 0;
    descriptors = EmptyMap(PosixFileDescriptors);
    open_descriptions = EmptyMap(PosixOpenDescriptions);
    next_description = 1;
    standard_descriptor_flags = (PosixStandardDescriptorFlags){0};
    standard_descriptor_open[0] = true;
    standard_descriptor_open[1] = true;
    standard_descriptor_open[2] = true;
    opened_handle = IO_SESSION_HANDLE_BASE + 19;
    opened_path = empty_bstr();
    opened_flags = 0;
    closed_handle = 0;
    next_read_result = IO_EOF;
    next_write_result = 0;
    next_pipe = (IOPipe){.read = IO_SESSION_HANDLE_BASE + 20, .write = IO_SESSION_HANDLE_BASE + 21};
    last_read_flags = 0;
    last_write_flags = 0;
    standard_status_flags = (PosixStandardStatusFlags){
        .standard_input = O_RDONLY,
        .standard_output = O_WRONLY,
        .standard_error = O_WRONLY,
    };
    terminal_descriptor = -1;
    terminal_path = empty_bstr();
    fork_inheritances = EmptyMap(PosixForkInheritances);
    inheritance_prepares = 0;
    inheritance_commits = 0;
    inheritance_aborts = 0;
    inheritance_claims = 0;
    trace_resets = 0;
    inheritance_commit_pid = 0;
    current_pid = 51;
    errno = 0;
}

void tearDown(void)
{
}

void test_posix_open_maps_native_handle_to_small_descriptor(void)
{
    int descriptor = open("etc/config", O_RDONLY);

    TEST_ASSERT_EQUAL_INT(3, descriptor);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/etc/config"), opened_path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, opened_flags);
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    TEST_ASSERT_EQUAL_INT64(IO_SESSION_HANDLE_BASE + 19, closed_handle);
}

void test_posix_open_maps_exclusive_create_and_existing_file_error(void)
{
    opened_handle = IO_ALREADY_EXISTS;

    TEST_ASSERT_EQUAL_INT(-1, open("new", O_WRONLY | O_CREAT | O_EXCL, 0600));
    TEST_ASSERT_EQUAL_BSTRING(bstr("/new"), opened_path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE, opened_flags);
    TEST_ASSERT_EQUAL_INT(EEXIST, errno);
}

void test_posix_truncation_is_an_open_flag_and_never_removes_the_path(void)
{
    int descriptor = open("file", O_WRONLY);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE, opened_flags);
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    descriptor = open("file", O_RDWR | O_TRUNC);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_TRUNCATE, opened_flags);
    TEST_ASSERT_EQUAL_UINT(0, remove_calls);
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    TEST_ASSERT_EQUAL_INT(-1, open("file", O_RDONLY | O_TRUNC));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_UINT(0, remove_calls);
}

void test_posix_read_converts_native_eof_to_zero(void)
{
    int descriptor = open("file", O_RDONLY);

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(0, read(descriptor, buffer, sizeof(buffer)));
    close(descriptor);
}

void test_posix_pipe_allocates_read_and_write_descriptors(void)
{
    int pipe_descriptors[2];

    TEST_ASSERT_EQUAL_INT(0, pipe(pipe_descriptors));
    TEST_ASSERT_EQUAL_INT(3, pipe_descriptors[0]);
    TEST_ASSERT_EQUAL_INT(4, pipe_descriptors[1]);
    TEST_ASSERT_EQUAL_INT64(next_pipe.read, PosixIO__native_handle(pipe_descriptors[0]));
    TEST_ASSERT_EQUAL_INT64(next_pipe.write, PosixIO__native_handle(pipe_descriptors[1]));
    TEST_ASSERT_EQUAL_INT(O_RDONLY, fcntl(pipe_descriptors[0], F_GETFL));
    TEST_ASSERT_EQUAL_INT(O_WRONLY, fcntl(pipe_descriptors[1], F_GETFL));
}

void test_posix_pipe_maps_creation_failure_and_null_output(void)
{
    next_pipe.read = IO_NO_DEVICE_FOUND;
    int pipe_descriptors[2];
    TEST_ASSERT_EQUAL_INT(-1, pipe(pipe_descriptors));
    TEST_ASSERT_EQUAL_INT(ENOENT, errno);

    TEST_ASSERT_EQUAL_INT(-1, pipe(NULL));
    TEST_ASSERT_EQUAL_INT(EFAULT, errno);
}

void test_posix_write_maps_broken_pipe_to_epipe(void)
{
    int descriptor = open("file", O_WRONLY);
    next_write_result = IO_BROKEN_PIPE;

    TEST_ASSERT_EQUAL_INT64(-1, write(descriptor, "x", 1));
    TEST_ASSERT_EQUAL_INT(EPIPE, errno);
}

void test_posix_dup_shares_open_description_until_last_close(void)
{
    int descriptor = open("file", O_RDONLY);
    int duplicate = dup(descriptor);

    TEST_ASSERT_EQUAL_INT(4, duplicate);
    TEST_ASSERT_EQUAL_INT64(PosixIO__native_handle(descriptor), PosixIO__native_handle(duplicate));
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    TEST_ASSERT_EQUAL_INT64(0, closed_handle);
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(duplicate));
    TEST_ASSERT_EQUAL_INT(0, close(duplicate));
    TEST_ASSERT_EQUAL_INT64(opened_handle, closed_handle);
}

void test_posix_fork_inherits_each_open_description_once(void)
{
    int descriptor = open("shared", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(4, dup(descriptor));

    TEST_ASSERT_EQUAL(IO_OK, PosixIO__prepare_fork());
    TEST_ASSERT_EQUAL_UINT(1, inheritance_prepares);
    TEST_ASSERT_EQUAL(IO_OK, PosixIO__commit_fork(42));
    TEST_ASSERT_EQUAL_UINT(1, inheritance_commits);
    TEST_ASSERT_EQUAL(IO_OK, PosixIO__claim_fork());
    TEST_ASSERT_EQUAL_UINT(1, inheritance_claims);
    TEST_ASSERT_EQUAL_UINT(1, trace_resets);
    TEST_ASSERT_EQUAL_UINT(0, inheritance_aborts);
}

void test_posix_exec_payload_inherits_each_non_cloexec_open_description_once(void)
{
    int descriptor = open("shared", O_RDONLY);
    int duplicate = dup(descriptor);
    int cloexec = dup(descriptor);
    TEST_ASSERT_EQUAL_INT(0, fcntl(cloexec, F_SETFD, FD_CLOEXEC));

    char * arguments[] = {"tool", NULL};
    ExecPayloadPtr payload = PosixIO__prepare_exec_payload("/bin/tool", "/work", arguments, NULL);

    TEST_ASSERT_NOT_NULL(payload);
    TEST_ASSERT_EQUAL_UINT(1, inheritance_prepares);
    TEST_ASSERT_EQUAL_UINT64(1, ExecPayload__open_description_count(payload));
    TEST_ASSERT_EQUAL_UINT64(2, ExecPayload__descriptor_count(payload));
    TEST_ASSERT_EQUAL_INT(descriptor, ExecPayload__descriptor_at(payload, 0)->descriptor);
    TEST_ASSERT_EQUAL_INT(duplicate, ExecPayload__descriptor_at(payload, 1)->descriptor);
    TEST_ASSERT_EQUAL(IO_OK, PosixIO__commit_exec());
    TEST_ASSERT_EQUAL_UINT(1, inheritance_commits);
    TEST_ASSERT_EQUAL_UINT64(current_pid, inheritance_commit_pid);
    PosixExecPayload__dispose(payload);
}

void test_posix_exec_claim_rebuilds_duplicate_descriptors(void)
{
    int descriptor = open("shared", O_RDONLY);
    int duplicate = dup(descriptor);
    char * arguments[] = {"tool", NULL};
    ExecPayloadPtr payload = PosixIO__prepare_exec_payload("/bin/tool", "/work", arguments, NULL);
    TEST_ASSERT_NOT_NULL(payload);

    descriptors = EmptyMap(PosixFileDescriptors);
    open_descriptions = EmptyMap(PosixOpenDescriptions);
    TEST_ASSERT_EQUAL(IO_OK, PosixIO__claim_exec(payload));
    TEST_ASSERT_EQUAL_UINT(1, inheritance_claims);
    TEST_ASSERT_EQUAL_UINT(1, trace_resets);
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(descriptor));
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(duplicate));
    TEST_ASSERT_EQUAL_INT(O_RDONLY, fcntl(descriptor, F_GETFL));
    PosixExecPayload__dispose(payload);
}

void test_posix_terminal_identity_is_preserved_across_standard_descriptor_duplication(void)
{
    terminal_descriptor = STDIN_FILENO;
    terminal_path = bstr(":tty:");

    int duplicate = dup(STDIN_FILENO);
    TEST_ASSERT_EQUAL_INT(3, duplicate);
    TEST_ASSERT_EQUAL_BSTRING(bstr(":tty:"), unwrap_optional(PosixIO__terminal_path(duplicate), empty_bstr()));
    TEST_ASSERT_EQUAL_INT(0, close(STDIN_FILENO));
    TEST_ASSERT_EQUAL_BSTRING(bstr(":tty:"), unwrap_optional(PosixIO__terminal_path(duplicate), empty_bstr()));
}

void test_posix_terminal_identity_recognizes_numbered_console_nodes(void)
{
    int descriptor = open("/dev/tty0", O_RDWR);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, descriptor);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/tty0"), unwrap_optional(PosixIO__terminal_path(descriptor), empty_bstr()));
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    descriptor = open("/dev/vtty12", O_RDWR);
    TEST_ASSERT_GREATER_OR_EQUAL_INT(3, descriptor);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/vtty12"), unwrap_optional(PosixIO__terminal_path(descriptor), empty_bstr()));
}

void test_posix_terminal_identity_rejects_regular_files(void)
{
    int regular = open("file", O_RDONLY);
    TEST_ASSERT_FALSE(is_something(PosixIO__terminal_path(regular)));
    TEST_ASSERT_EQUAL_INT(ENOTTY, errno);
}

void test_posix_dup2_replaces_standard_descriptor_and_clears_close_on_exec(void)
{
    int descriptor = open("file", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_SETFD, FD_CLOEXEC));

    TEST_ASSERT_EQUAL_INT(STDIN_FILENO, dup2(descriptor, STDIN_FILENO));
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(STDIN_FILENO));
    TEST_ASSERT_EQUAL_INT(0, fcntl(STDIN_FILENO, F_GETFD));
    TEST_ASSERT_EQUAL_INT(0, close(descriptor));
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(STDIN_FILENO));
}

void test_posix_dup2_same_descriptor_is_a_noop(void)
{
    int descriptor = open("file", O_RDONLY);

    TEST_ASSERT_EQUAL_INT(descriptor, dup2(descriptor, descriptor));
    TEST_ASSERT_EQUAL_INT(-1, dup2(99, 99));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
}

void test_posix_fcntl_dupfd_uses_requested_minimum(void)
{
    int descriptor = open("file", O_RDONLY);

    int duplicate = fcntl(descriptor, F_DUPFD, 10);
    TEST_ASSERT_EQUAL_INT(10, duplicate);
    TEST_ASSERT_EQUAL_INT64(opened_handle, PosixIO__native_handle(duplicate));
    TEST_ASSERT_EQUAL_INT(0, fcntl(duplicate, F_GETFD));
}

void test_posix_read_converts_native_failure_to_errno(void)
{
    int descriptor = open("file", O_RDONLY);
    next_read_result = IO_PERMISSION_DENIED;

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(-1, read(descriptor, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(EACCES, errno);
    close(descriptor);
}

void test_posix_read_maps_interrupted_stream_to_eintr(void)
{
    int descriptor = open("file", O_RDONLY);
    next_read_result = IO_INTERRUPTED;

    char buffer[4];
    TEST_ASSERT_EQUAL_INT64(-1, read(descriptor, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(EINTR, errno);
    close(descriptor);
}

void test_posix_close_recycles_descriptor(void)
{
    int first = open("first", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(0, close(first));

    opened_handle++;
    int second = open("second", O_RDONLY);
    TEST_ASSERT_EQUAL_INT(first, second);
    close(second);
}

void test_posix_fsync_accepts_open_descriptor_and_rejects_unknown_descriptor(void)
{
    int descriptor = open("file", O_WRONLY);

    TEST_ASSERT_EQUAL_INT(0, fsync(descriptor));
    TEST_ASSERT_EQUAL_INT(-1, fsync(descriptor + 1));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    close(descriptor);
}

void test_posix_fcntl_gets_sets_and_clears_close_on_exec(void)
{
    int descriptor = open("file", O_RDONLY);

    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_GETFD));
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_SETFD, FD_CLOEXEC));
    TEST_ASSERT_EQUAL_INT(FD_CLOEXEC, fcntl(descriptor, F_GETFD));
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_SETFD, 0));
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_GETFD));
    close(descriptor);
}

void test_posix_fcntl_tracks_standard_descriptor_flags(void)
{
    TEST_ASSERT_EQUAL_INT(0, fcntl(STDERR_FILENO, F_SETFD, FD_CLOEXEC));
    TEST_ASSERT_EQUAL_INT(FD_CLOEXEC, fcntl(STDERR_FILENO, F_GETFD));
}

void test_posix_fcntl_status_flags_are_shared_across_duplicates(void)
{
    int descriptor = open("file", O_RDONLY | O_APPEND);
    int duplicate = dup(descriptor);

    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_APPEND, fcntl(descriptor, F_GETFL));
    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_APPEND, fcntl(duplicate, F_GETFL));
    TEST_ASSERT_EQUAL_INT(0, fcntl(duplicate, F_SETFL, O_WRONLY | O_NONBLOCK | O_APPEND));
    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_NONBLOCK | O_APPEND, fcntl(descriptor, F_GETFL));
    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_NONBLOCK | O_APPEND, fcntl(duplicate, F_GETFL));
    TEST_ASSERT_EQUAL_INT(0, fcntl(duplicate, F_SETFL, O_NONBLOCK));
    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_NONBLOCK, fcntl(descriptor, F_GETFL));
    TEST_ASSERT_EQUAL_INT(O_RDONLY | O_NONBLOCK, fcntl(duplicate, F_GETFL));
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_GETFD));
    TEST_ASSERT_EQUAL_INT(0, fcntl(duplicate, F_SETFD, FD_CLOEXEC));
    TEST_ASSERT_EQUAL_INT(0, fcntl(descriptor, F_GETFD));
    TEST_ASSERT_EQUAL_INT(FD_CLOEXEC, fcntl(duplicate, F_GETFD));
}

void test_posix_nonblocking_status_is_carried_on_reads_and_writes(void)
{
    int reader = open("input", O_RDONLY | O_NONBLOCK);
    int writer = open("output", O_WRONLY);
    char buffer[1];
    next_read_result = IO_WOULD_BLOCK;

    TEST_ASSERT_EQUAL_INT64(-1, read(reader, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
    TEST_ASSERT_EQUAL_UINT64(IO_TRANSFER_NONBLOCK, last_read_flags);

    TEST_ASSERT_EQUAL_INT(0, fcntl(writer, F_SETFL, O_NONBLOCK));
    next_write_result = IO_WOULD_BLOCK;
    TEST_ASSERT_EQUAL_INT64(-1, write(writer, "x", 1));
    TEST_ASSERT_EQUAL_INT(EAGAIN, errno);
    TEST_ASSERT_EQUAL_UINT64(IO_TRANSFER_NONBLOCK, last_write_flags);
}

void test_posix_append_status_is_carried_only_on_writes(void)
{
    int descriptor = open("output", O_RDWR | O_APPEND);
    char buffer[1];

    TEST_ASSERT_EQUAL_INT64(0, read(descriptor, buffer, sizeof(buffer)));
    TEST_ASSERT_EQUAL_UINT64(0, last_read_flags);
    TEST_ASSERT_EQUAL_INT64(0, write(descriptor, "x", 1));
    TEST_ASSERT_EQUAL_UINT64(IO_TRANSFER_APPEND, last_write_flags);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ | IO_OPEN_WRITE, opened_flags);
}

void test_posix_fcntl_rejects_closed_descriptor(void)
{
    int descriptor = open("file", O_RDONLY);
    close(descriptor);

    TEST_ASSERT_EQUAL_INT(-1, fcntl(descriptor, F_GETFD));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    TEST_ASSERT_EQUAL_INT(-1, fcntl(descriptor, F_SETFD, FD_CLOEXEC));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    TEST_ASSERT_EQUAL_INT(-1, fcntl(descriptor, F_GETFL));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
    TEST_ASSERT_EQUAL_INT(-1, fcntl(descriptor, F_SETFL, O_NONBLOCK));
    TEST_ASSERT_EQUAL_INT(EBADF, errno);
}

void test_posix_fcntl_rejects_unsupported_command(void)
{
    int descriptor = open("file", O_RDONLY);

    TEST_ASSERT_EQUAL_INT(-1, fcntl(descriptor, 999));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    close(descriptor);
}

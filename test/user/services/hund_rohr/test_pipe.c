#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "../../../../user/services/hund-rohr/pipe.h"
#include "sys/mem.h"

static PipeService pipes;
static uint8_t transfer[PIPE_CAPACITY + PIPE_ATOMIC_WRITE];

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

void setUp(void)
{
    pipes = PipeService__new();
    memset(transfer, 0, sizeof(transfer));
}

void tearDown(void)
{
    PipeService__release(&pipes, 1);
    PipeService__release(&pipes, 2);
}

static IOReply execute(QueueConnectionID session, uint64_t pid, IORequest request)
{
    request.client_session = session;
    request.client_pid = pid;
    return PipeService__execute(&pipes, &request);
}

static IOPipe create_pipe(QueueConnectionID session, uint64_t pid)
{
    IORequest request = IORequest__new(IO_REQUEST_PIPE_CREATE, 0);
    request.flags = IO_OPEN_READ | IO_OPEN_WRITE;
    request.buffered = true;
    IOReply reply = execute(session, pid, request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    return reply.pipe;
}

static IOReply transfer_request(QueueConnectionID session, uint64_t pid, IORequestOperation operation, IOHandle handle,
                                size_t count)
{
    IORequest request = IORequest__new(operation, handle);
    request.count = count;
    request.transfer_address = (Address)transfer;
    return execute(session, pid, request);
}

static IOReply nonblocking_transfer_request(QueueConnectionID session, uint64_t pid, IORequestOperation operation,
                                            IOHandle handle, size_t count)
{
    IORequest request = IORequest__new(operation, handle);
    request.count = count;
    request.transfer_address = (Address)transfer;
    request.flags = IO_TRANSFER_NONBLOCK;
    return execute(session, pid, request);
}

void test_pipe_transfers_bytes_in_order_across_ring_wrap(void)
{
    IOPipe pipe = create_pipe(1, 101);
    memcpy(transfer, "abcd", 4);
    TEST_ASSERT_EQUAL_INT64(4, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, 4).value);
    TEST_ASSERT_EQUAL_INT64(2, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 2).value);
    TEST_ASSERT_EQUAL_MEMORY("ab", transfer, 2);

    memcpy(transfer, "efg", 3);
    TEST_ASSERT_EQUAL_INT64(3, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, 3).value);
    TEST_ASSERT_EQUAL_INT64(5, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 5).value);
    TEST_ASSERT_EQUAL_MEMORY("cdefg", transfer, 5);
}

void test_pipe_empty_read_waits_until_writer_closes_then_returns_eof(void)
{
    IOPipe pipe = create_pipe(1, 101);

    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(1, 101, IORequest__new(IO_REQUEST_CLOSE, pipe.write)).status);
    TEST_ASSERT_EQUAL(IO_EOF, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).status);
}

void test_pipe_empty_nonblocking_read_returns_would_block_but_closed_writer_returns_eof(void)
{
    IOPipe pipe = create_pipe(1, 101);

    TEST_ASSERT_EQUAL(IO_WOULD_BLOCK, nonblocking_transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).status);
    TEST_ASSERT_EQUAL(IO_OK, execute(1, 101, IORequest__new(IO_REQUEST_CLOSE, pipe.write)).status);
    TEST_ASSERT_EQUAL(IO_EOF, nonblocking_transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).status);
}

void test_pipe_atomic_write_waits_for_enough_space(void)
{
    IOPipe pipe = create_pipe(1, 101);
    memset(transfer, 42, PIPE_CAPACITY);
    TEST_ASSERT_EQUAL_INT64(PIPE_CAPACITY, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, PIPE_CAPACITY).value);
    TEST_ASSERT_EQUAL_INT64(1, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).value);

    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA,
                      transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, PIPE_ATOMIC_WRITE).status);
    TEST_ASSERT_EQUAL_INT64(1, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, PIPE_ATOMIC_WRITE + 1).value);
}

void test_pipe_nonblocking_atomic_write_returns_would_block_without_enough_space(void)
{
    IOPipe pipe = create_pipe(1, 101);
    memset(transfer, 42, PIPE_CAPACITY);
    TEST_ASSERT_EQUAL_INT64(PIPE_CAPACITY, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, PIPE_CAPACITY).value);
    TEST_ASSERT_EQUAL_INT64(1, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).value);

    TEST_ASSERT_EQUAL(IO_WOULD_BLOCK,
                      nonblocking_transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, PIPE_ATOMIC_WRITE).status);
}

void test_pipe_rejects_unknown_transfer_flags(void)
{
    IOPipe pipe = create_pipe(1, 101);
    IORequest request = IORequest__new(IO_REQUEST_READ, pipe.read);
    request.count = 1;
    request.transfer_address = (Address)transfer;
    request.flags = IO_TRANSFER_NONBLOCK << 1;

    TEST_ASSERT_EQUAL(IO_OPERATION_NOT_SUPPORTED, execute(1, 101, request).status);
}

void test_pipe_ignores_append_on_writes(void)
{
    IOPipe pipe = create_pipe(1, 101);
    transfer[0] = 42;
    IORequest request = IORequest__new(IO_REQUEST_WRITE, pipe.write);
    request.count = 1;
    request.transfer_address = (Address)transfer;
    request.flags = IO_TRANSFER_APPEND;

    TEST_ASSERT_EQUAL_UINT64(1, execute(1, 101, request).value);
    TEST_ASSERT_EQUAL_UINT64(1, transfer_request(1, 101, IO_REQUEST_READ, pipe.read, 1).value);
    TEST_ASSERT_EQUAL_UINT8(42, transfer[0]);
}

void test_pipe_write_without_readers_reports_broken_pipe(void)
{
    IOPipe pipe = create_pipe(1, 101);
    TEST_ASSERT_EQUAL(IO_OK, execute(1, 101, IORequest__new(IO_REQUEST_CLOSE, pipe.read)).status);

    TEST_ASSERT_EQUAL(IO_BROKEN_PIPE, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.write, 1).status);
}

void test_pipe_endpoints_are_authenticated_and_directional(void)
{
    IOPipe pipe = create_pipe(1, 101);

    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, transfer_request(2, 101, IO_REQUEST_READ, pipe.read, 1).status);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_HANDLE, transfer_request(1, 102, IO_REQUEST_READ, pipe.read, 1).status);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, transfer_request(1, 101, IO_REQUEST_WRITE, pipe.read, 1).status);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, transfer_request(1, 101, IO_REQUEST_READ, pipe.write, 1).status);
}

void test_pipe_release_closes_every_endpoint_owned_by_session(void)
{
    create_pipe(1, 101);
    create_pipe(2, 102);
    TEST_ASSERT_EQUAL_UINT64(4, PipeEndpoints__len(&pipes.endpoints));
    TEST_ASSERT_EQUAL_UINT64(2, PipeObjects__len(&pipes.pipes));

    PipeService__release(&pipes, 1);
    TEST_ASSERT_EQUAL_UINT64(2, PipeEndpoints__len(&pipes.endpoints));
    TEST_ASSERT_EQUAL_UINT64(1, PipeObjects__len(&pipes.pipes));
}

void test_pipe_creation_requires_directional_shared_buffers(void)
{
    IORequest request = IORequest__new(IO_REQUEST_PIPE_CREATE, 0);
    request.flags = IO_OPEN_READ | IO_OPEN_WRITE;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(1, 101, request).status);

    request.buffered = true;
    request.flags = IO_OPEN_READ;
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, execute(1, 101, request).status);
}

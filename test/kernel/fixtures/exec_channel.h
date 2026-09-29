#ifndef TEST_EXEC_CHANNEL_H
#define TEST_EXEC_CHANNEL_H
#include "../../../kernel/exec/load.h"
#include "../../../kernel/syscall/exec_handler.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "process.h"
OptionalDef(Queue);

typedef struct FakeExecService
{
    ProcessPtr warden;
    ProcessPtr io;
    ProcessPtr puffer;
    BufferID buffer;
    Optional(Queue) control;
    Address payload_address;
    ChannelPayloadPtr payload;
    Address image;
    size_t image_size;
    size_t offset;
    unsigned reads;
    bool redirect_open;
    bool redirected;
} FakeExecService;

typedef FakeExecService * FakeExecServicePtr;

static FakeExecService FakeExecService__new(KernelPtr kernel)
{
    ProcessPtr warden = dummy_process(100, 0, PROC_STATE_NEW);
    ProcessPtr io = dummy_process(101, 0, PROC_STATE_NEW);
    ProcessPtr puffer = dummy_process(102, 0, PROC_STATE_NEW);
    puffer->name = bstr("/sbin/services/hunde-puffer");
    puffer->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(puffer);
    Scheduler__schedule(kernel->scheduler, puffer);
    match_result(
        Buffers__operation(&kernel->scheduler->buffers, kernel->scheduler, puffer, MEM_BUFFER_REGISTER, 0, 0, 0, 0),
        success_do_nothing(),
        error_test_fail()
    );
    warden->process_class = PROCESS_CLASS_SERVICE;
    io->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(warden);
    Process__set_hund_identity(io);
    Scheduler__schedule(kernel->scheduler, warden);
    Scheduler__schedule(kernel->scheduler, io);
    match_result(Queues__listen(&kernel->scheduler->queues, warden, &bstr(CHANNEL_WARDEN_SERVICE_NAME)),
                 success_do_nothing(),
        error_test_fail()
    );
    match_result(Queues__listen(&kernel->scheduler->queues, io, &bstr(IO_SYSTEM_SERVICE)),
        success_do_nothing(),
                 error_test_fail()
    );
    return (FakeExecService){.warden = warden, .io = io, .puffer = puffer, .control = None(Queue)};
}

static void FakeExecService__drain(FakeExecServicePtr self, KernelPtr kernel)
{
    match_optional(
        self->control,
        some(control)
        {
            if (Queue__status(&control) == QUEUE_CLOSED)
            {
                Queues__close(&kernel->scheduler->queues, self->warden, Queue__connection(&control));
                if (self->payload_address != 0)
                {
                    Scheduler__release_shared_page(kernel->scheduler, self->warden, self->payload_address);
                }
                self->payload_address = 0;
                if (self->buffer != 0)
                {
                    Buffers__drop(&kernel->scheduler->buffers, kernel->scheduler, self->buffer, self->io->pid);
                    self->buffer = 0;
                }
                self->control = None(Queue);
            }
        },
        none_do_nothing()
    );
}

static void FakeExecService__poll(FakeExecServicePtr self, KernelPtr kernel, ProcessPtr parent)
{
    FakeExecService__drain(self, kernel);
    ExecLoadPtr load;
    match_optional(parent->exec_load,
        some(value)
        {
            load = value;
        },
        none_test_fail()
    );
    if (is_nothing(self->control))
    {
        QueueConnectionID id = Queue__connection(&load->channel.channel.control);
        match_result(Queues__accept(&kernel->scheduler->queues, self->warden, id),
            success_do_nothing(),
                     error_test_fail()
        );
        match_optional(
            QueueConnections__at(&kernel->scheduler->queues.connections, id % QUEUE_LIMIT),
            some(connection)
            {
                Address base = connection.memory.kernel_addr.virtual_addr;
                match_result(
                    Queue__attach((QueueIdentityPtr)base, (QueuePagePtr)(base + QUEUE_PAGE_SIZE),
                                  (QueuePagePtr)(base + 2 * QUEUE_PAGE_SIZE), id, self->warden->pid,
                                  k_v_ptr(connection.client_doorbell, IpcDoorbell), &Queue__ignore_data_available),
                    success(control)
                    {
                        self->control = Some(Queue, control);
                    },
                    error_test_fail()
                );
            },
            none_test_fail()
        );
    }
    match_optional(
        self->control,
        some(control)
        {
            match_result(
                Queue__pop(&control),
                success(message)
                {
                    TEST_ASSERT_EQUAL_UINT64(QUEUE_KERNEL_PID, message.sender_pid);
                    QueueMessage response = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, sizeof(IOReply),
                                                                       Queue__connection(&control));
                    response.request = message.request;
                    if (message.kind == CHANNEL_REQUEST_OPEN)
                    {
                        match_result(
                            Scheduler__alloc_shared_page(kernel->scheduler, self->warden, QUEUE_KERNEL_PID,
                                                         self->io->pid),
                            success(address)
                            {
                                self->payload_address = address;
                            },
                            error_test_fail()
                        );
                        match_optional(
                            ManagedSharedPages__at(&kernel->scheduler->shared_pages,
                                                   (self->payload_address - MEM_SHARED_PAGE_BASE) /
                                                       MEM_SHARED_PAGE_SIZE),
                            some(page)
                            {
                                self->payload = k_v_ptr(page.address, ChannelPayload);
                            },
                            none_test_fail()
                        );
                        response = ChannelControlMessage__new(CHANNEL_RESPONSE_OPEN, self->payload_address, 0,
                                                              Queue__connection(&control));
                    }
                    else if (message.kind == CHANNEL_REQUEST_SUBMIT)
                    {
                        TEST_ASSERT_EQUAL_UINT64(parent->pid, message.request.origin_pid);
                        IORequest request;
                        TEST_ASSERT_TRUE(
                            ChannelPayload__read_request(self->payload, (ChannelDataPtr)&request, sizeof(request)));
                        TEST_ASSERT_EQUAL_UINT64(parent->pid, request.client_pid);
                        IOReply reply = IOReply__new(IO_OK, 0);
                        if (request.operation == IO_REQUEST_OPEN)
                        {
                            BString path = IORequest__path(&request);
                            if (self->redirect_open && !self->redirected)
                            {
                                reply = IOReply__redirect(&bstr(IO_SYSTEM_SERVICE), &bstr("/hello_world"),
                                                          &empty_bstr(), true);
                                self->redirected = true;
                                TEST_ASSERT_TRUE(ChannelPayload__write_reply(self->payload, (ConstChannelDataPtr)&reply,
                                                                             sizeof(reply)));
                                match_result(Queue__push(&control, &response),
                                    success_do_nothing(),
                                    error_test_fail()
                                );
                                self->control = Some(Queue, control);
                                return;
                            }
                            StringPos name_start = BString__starts_with(&path, &bstr("/sbin/")) ? 6 : 1;
                            BString name = BString__slice(&path, name_start, BString__length(&path));
                            match_optional(
                                EmbeddedElf__load(&name),
                                some(elf)
                                {
                                    self->image = elf.start;
                                    self->image_size = elf.end - elf.start;
                                    self->offset = 0;
                                    reply.value = IO_FILESYSTEM_HANDLE_BASE;
                                    match_result(
                                        Buffers__operation(&kernel->scheduler->buffers, kernel->scheduler, self->puffer,
                                                           MEM_BUFFER_CREATE, self->io->pid, QUEUE_KERNEL_PID,
                                                           BUFFER_READ, 0),
                                        success(id)
                                        {
                                            self->buffer = id;
                                            reply.buffers.read = BufferDescriptor__new(id, BUFFER_READ);
                                        },
                                        error_test_fail()
                                    );
                                },
                                none()
                                {
                                    reply.status = IO_INVALID_PATH;
                                }
                            );
                        }
                        else if (request.operation == IO_REQUEST_READ)
                        {
                            size_t remaining = self->image_size - self->offset;
                            size_t size = remaining < BUFFER_SIZE ? remaining : BUFFER_SIZE;
                            TEST_ASSERT_EQUAL_UINT64(self->buffer, request.buffer);
                            match_result(
                                Buffers__kernel_read(&kernel->scheduler->buffers, self->buffer, self->io->pid, size),
                                success(address)
                                {
                                    Kernel__memcopy(self->image + self->offset, address, size);
                                },
                                error_test_fail()
                            );
                            self->offset += size;
                            reply.value = size;
                            self->reads++;
                        }
                        TEST_ASSERT_TRUE(
                            ChannelPayload__write_reply(self->payload, (ConstChannelDataPtr)&reply, sizeof(reply)));
                    }
                    else
                    {
                        TEST_ASSERT_EQUAL(CHANNEL_REQUEST_ACKNOWLEDGE, message.kind);
                        self->control = Some(Queue, control);
                        return;
                    }
                    match_result(Queue__push(&control, &response),
                        success_do_nothing(),
                        error_test_fail()
                    );
                },
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(QUEUE_EMPTY, code);
                }
            );
            self->control = Some(Queue, control);
        },
        none_test_fail()
    );
}

static Result(SystemCallResult)
    FakeExecService__spawn(FakeExecServicePtr self, KernelPtr kernel, OptionalPtr(ProcessPtr) process, Anything request)
{
    Result(SystemCallResult) result = ExecRequest__handler(kernel, process, request, 0, 0, 0, 0);
    match_result(
        result,
        success(value)
        {
            return result;
        },
        error(code, message)
        {
            if (code != SYSCALL_IO_BLOCK)
            {
                return result;
            }
        }
    );
    bool deferred = false;
    match_optional(
        *process,
        some(parent)
        {
            deferred = !is_nothing(parent->exec_load) &&
                       Process__has_parked_operation(parent, PARK_WAIT_EXEC_LOAD, &None(OperationKey));
        },
        none_do_nothing()
    );
    if (!deferred)
    {
        return result;
    }
    for (unsigned attempt = 0; attempt < 100000; attempt++)
    {
        match_optional(
            *process,
            some(parent)
            {
                if (!is_nothing(parent->exec_load))
                {
                    FakeExecService__poll(self, kernel, parent);
                    Scheduler__preempt(kernel->scheduler, 0, &None(ProcessContextPtr));
                    continue;
                }
                FakeExecService__drain(self, kernel);
                match_result(
                    deferred_syscall_result(),
                    success(value)
                    {
                        if ((int64_t)value < 0)
                        {
                            return Error(SystemCallResult, bstr("Deferred executable load failed"), (int64_t)value);
                        }
                        return Success(SystemCallResult, value);
                    },
                    error(code, message)
                    {
                        ProcessTable processes = Scheduler__process_snapshot(kernel->scheduler);
                        for_each_fa(
                            ProcessTable,
                            processes,
                            index,
                            child,
                            {
                                match_optional(
                                    child->parent,
                                    some(child_parent)
                                    {
                                        if (child_parent == parent)
                                        {
                                            return Success(SystemCallResult, child->pid);
                                        }
                                    },
                                    none_do_nothing()
                                );
                            }
                        );
                        return Error(SystemCallResult, message, code);
                    }
                );
            },
            none_test_fail()
        );
    }
    TEST_FAIL_MESSAGE("Executable Channel failed to complete");
    return result;
}
#endif

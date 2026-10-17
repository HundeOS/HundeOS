#ifndef TEST_EXEC_CHANNEL_H
#define TEST_EXEC_CHANNEL_H
#include "../../../kernel/exec/load.h"
#include "../../../kernel/syscall/exec_handler.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "process.h"
OptionalDef(Queue);

typedef struct FakeExecService
{
    ProcessPtr io;
    ProcessPtr puffer;
    BufferID buffer;
    Optional(Queue) control;
    Address image;
    size_t image_size;
    size_t offset;
    unsigned reads;
    unsigned opens;
    BString first_path;
    BString second_path;
    BString override_path;
    Address override_image;
    size_t override_image_size;
    bool redirect_open;
    bool redirected;
} FakeExecService;

typedef FakeExecService * FakeExecServicePtr;

static FakeExecService FakeExecService__new(KernelPtr kernel)
{
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
    io->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(io);
    Scheduler__schedule(kernel->scheduler, io);
    match_result(Queues__listen(&kernel->scheduler->queues, io, &bstr(IO_SYSTEM_SERVICE)),
        success_do_nothing(),
                 error_test_fail()
    );
    return (FakeExecService){.io = io, .puffer = puffer, .control = None(Queue)};
}

static void FakeExecService__drain(FakeExecServicePtr self, KernelPtr kernel)
{
    match_optional(
        self->control,
        some(control)
        {
            if (Queue__status(&control) == QUEUE_CLOSED)
            {
                Queues__close(&kernel->scheduler->queues, self->io, Queue__connection(&control));
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

static Optional(Elf) FakeExecService__image(FakeExecServicePtr self, ConstBStringPtr path, ConstBStringPtr name)
{
    if (self->override_image != 0 && BString__equals(path, &self->override_path))
    {
        return Some(Elf, Elf__new(self->override_image, self->override_image + self->override_image_size, path));
    }
    if (BString__equals(path, &bstr(WUFF_INTERPRETER_PATH)))
    {
        return EmbeddedElf__load(&bstr("hello_world"));
    }
    return EmbeddedElf__load(name);
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
        match_result(Queues__accept(&kernel->scheduler->queues, self->io, id),
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
                                  (QueuePagePtr)(base + 2 * QUEUE_PAGE_SIZE), id, self->io->pid,
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
                    TEST_ASSERT_EQUAL(CHANNEL_REQUEST_SUBMIT, message.kind);
                    QueueMessage response = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, sizeof(IOReply),
                                                                       Queue__connection(&control));
                    response.request = message.request;
                    TEST_ASSERT_EQUAL_UINT64(parent->pid, message.request.origin_pid);
                    IORequest request;
                    TEST_ASSERT_TRUE(Queue__read_data(&control, (ChannelDataPtr)&request, sizeof(request)));
                    TEST_ASSERT_EQUAL_UINT64(parent->pid, request.client_pid);
                    IOReply reply = IOReply__new(IO_OK, 0);
                    if (request.operation == IO_REQUEST_OPEN)
                    {
                        BString path = IORequest__path(&request);
                        self->opens++;
                        if (self->opens == 1)
                        {
                            self->first_path = path;
                        }
                        else if (self->opens == 2)
                        {
                            self->second_path = path;
                        }
                        if (self->redirect_open && !self->redirected)
                        {
                            reply =
                                IOReply__redirect(&bstr(IO_SYSTEM_SERVICE), &bstr("/hello_world"), &empty_bstr(), true);
                            self->redirected = true;
                            TEST_ASSERT_TRUE(
                                Queue__write_data(&control, (ConstChannelDataPtr)&reply, sizeof(reply), 0));
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
                            FakeExecService__image(self, &path, &name),
                            some(elf)
                            {
                                self->image = elf.start;
                                self->image_size = elf.end - elf.start;
                                self->offset = 0;
                                reply.value = IO_FILESYSTEM_HANDLE_BASE;
                                match_result(
                                    Buffers__operation(&kernel->scheduler->buffers, kernel->scheduler, self->puffer,
                                                       MEM_BUFFER_CREATE, self->io->pid, QUEUE_KERNEL_PID, BUFFER_READ,
                                                       0),
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
                    TEST_ASSERT_TRUE(Queue__write_data(&control, (ConstChannelDataPtr)&reply, sizeof(reply), 0));
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

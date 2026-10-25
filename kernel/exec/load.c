#include "load.h"
#include "../kalloc.h"

#define EXEC_IMAGE_LIMIT (64ULL * 1024 * 1024)
static BString load_failed = bstr("Executable loading failed");

static void ExecLoad__set_path(ExecLoadPtr self, ConstBStringPtr path)
{
    self->path_length = BString__copy_to(path, (char *)self->path_data, sizeof(self->path_data));
}

static BString ExecLoad__path(ExecLoadPtr self)
{
    return BString__from_chars((const char *)self->path_data,
                               self->path_length < IO_PROTOCOL_DATA_SIZE ? self->path_length : 0);
}

static BString ExecLoad__requested_path(ExecLoadPtr self)
{
    return BString__from_chars((const char *)self->requested_path_data,
                               self->requested_path_length < IO_PROTOCOL_DATA_SIZE ? self->requested_path_length : 0);
}

static void ExecLoad__dispose_finished(ProcessPtr process, ExecLoadPtr load)
{
    process->exec_load = None(ExecLoadPtr);
    if (load->buffer.valid)
    {
        Kernel__free(load->buffer.virtual_addr);
    }
    if (load->overlay_payload.valid)
    {
        Kernel__free(load->overlay_payload.virtual_addr);
    }
    Kernel__free((Address)load);
}

static Result(ChannelCode) ExecLoad__begin_interpreter(ExecLoadPtr self, SchedulerPtr scheduler, ProcessPtr process)
{
    if (self->buffer.valid)
    {
        Kernel__free(self->buffer.virtual_addr);
    }
    self->buffer = KernelAddress__new(0, 0, false);
    self->capacity = 0;
    self->size = 0;
    self->pending = false;
    self->route_final = false;
    self->redirects = 0;
    self->phase = EXEC_LOAD_OPEN;
    self->handle = 0;
    self->transfer = BufferDescriptor__new(0, BUFFER_READ);
    self->observation_valid = false;
    self->loading_interpreter = true;
    ExecLoad__set_path(self, &bstr(WUFF_INTERPRETER_PATH));
    match_result(
        KernelChannel__open(scheduler,
                            KernelServiceDirectory__kernel_directory(&self->context.kernel->service_directory),
                            &bstr(IO_SYSTEM_SERVICE), process),
        success(channel)
        {
            self->channel = channel;
            return Success(ChannelCode, CHANNEL_PENDING);
        },
        error(code, message)
        {
            return Error(ChannelCode, message, SPAWN_NO_FILE);
        }
    );
}

static ExecLoad ExecLoad__new(KernelChannel channel, ConstOriginatingContextPtr context, ConstBStringPtr path,
                              bool wait, OptionalPtr(UserID) user, ConstProcessArgumentsPtr arguments,
                              ConstSpawnStdioPtr stdio, OptionalPtr(KernelAddress) abort_report, bool overlay,
                              KernelAddress overlay_payload)
{
    ExecLoad self = {.channel = channel,
                     .context = *context,
                     .wait = wait,
                     .user = *user,
                     .arguments = *arguments,
                     .overlay_payload = overlay_payload,
                     .overlay = overlay,
                     .stdio = *stdio,
                     .abort_report = *abort_report,
                     .phase = EXEC_LOAD_OPEN};
    self.requested_path_length =
        BString__copy_to(path, (char *)self.requested_path_data, sizeof(self.requested_path_data));
    ExecLoad__set_path(&self, path);
    return self;
}

Result(ProcessPtr)
    ExecLoad__begin(ConstOriginatingContextPtr context, ConstBStringPtr path, bool wait, OptionalPtr(UserID) user,
                    ConstProcessArgumentsPtr arguments, ConstSpawnStdioPtr stdio,
                    OptionalPtr(KernelAddress) abort_report, bool overlay, KernelAddress overlay_payload)
{
    if (!Exec__can_execute(context, path) || !BString__starts_with(path, &bstr("/")) ||
        BString__length(path) >= IO_PROTOCOL_DATA_SIZE)
    {
        return Error(ProcessPtr, load_failed, SPAWN_FAILED);
    }
    match_optional(
        context->process,
        some(process)
        {
            if (!is_nothing(process->exec_load))
            {
                return Error(ProcessPtr, load_failed, SPAWN_FAILED);
            }
            KernelAddress allocation = Kernel__alloc(sizeof(ExecLoad));
            if (!allocation.valid)
            {
                return Error(ProcessPtr, load_failed, SPAWN_NO_MEM);
            }
            ExecLoadPtr load = (ExecLoadPtr)allocation.virtual_addr;
            SchedulerPtr scheduler = context->kernel->scheduler;
            critical_section(
                scheduler->scheduler_lock,
                {
                    match_result(
                        KernelChannel__open(
                            scheduler,
                            KernelServiceDirectory__kernel_directory(&context->kernel->service_directory),
                            &bstr(IO_SYSTEM_SERVICE), process),
                        success(channel)
                        {
                            *load = ExecLoad__new(channel, context, path, wait, user, arguments, stdio,
                                                  abort_report, overlay, overlay_payload);
                            process->exec_load = Some(ExecLoadPtr, load);
                        },
                        error(code, message)
                        {
                            Kernel__free(allocation.virtual_addr);
                            cs_return(Error(ProcessPtr, message, SPAWN_NO_FILE));
                        }
                    );
                }
            );
            return Error(ProcessPtr, bstr("Executable load pending"), SPAWN_PENDING);
        },
        none()
        {
            return Error(ProcessPtr, load_failed, SPAWN_FAILED);
        }
    );
}

static Result(ChannelCode) ExecLoad__append(ExecLoadPtr self, SchedulerPtr scheduler, IOReplyPtr reply)
{
    if (reply->value > BUFFER_SIZE || self->size > EXEC_IMAGE_LIMIT - reply->value)
    {
        return Error(ChannelCode, load_failed, SPAWN_FAILED);
    }
    size_t needed = self->size + reply->value;
    if (needed > self->capacity)
    {
        size_t capacity = self->capacity == 0 ? 65536 : self->capacity * 2;
        while (capacity < needed)
        {
            capacity *= 2;
        }
        KernelAddress buffer = Kernel__alloc(capacity);
        if (!buffer.valid)
        {
            return Error(ChannelCode, load_failed, SPAWN_NO_MEM);
        }
        if (self->buffer.valid)
        {
            Kernel__memcopy(self->buffer.virtual_addr, buffer.virtual_addr, self->size);
            Kernel__free(self->buffer.virtual_addr);
        }
        self->buffer = buffer;
        self->capacity = capacity;
    }
    if (reply->value > 0)
    {
        match_result(
            Buffers__kernel_read(&scheduler->buffers, self->transfer.id, self->channel.server_pid, reply->value),
            success(address)
            {
                Kernel__memcopy(address, self->buffer.virtual_addr + self->size, reply->value);
            },
            error(code, message)
            {
                return Error(ChannelCode, message, SPAWN_FAILED);
            }
        );
    }
    self->size = needed;
    return Success(ChannelCode, CHANNEL_OK);
}

static Result(ChannelCode)
    ExecLoad__redirect(ExecLoadPtr self, SchedulerPtr scheduler, ProcessPtr process, IOReplyPtr reply)
{
    if (self->redirects == IO_ROUTE_LIMIT)
    {
        return Error(ChannelCode, load_failed, SPAWN_FAILED);
    }
    BString service = IORoute__service(&reply->route);
    BString path = IORoute__path(&reply->route);
    if (BString__is_empty(&service) || BString__length(&path) >= IO_PROTOCOL_DATA_SIZE)
    {
        return Error(ChannelCode, load_failed, SPAWN_FAILED);
    }
    KernelChannel__close(&self->channel, scheduler);
    match_result(
        KernelChannel__open(scheduler,
                            KernelServiceDirectory__kernel_directory(&self->context.kernel->service_directory),
                            &service, process),
        success(channel)
        {
            self->channel = channel;
            ExecLoad__set_path(self, &path);
            self->route_final = reply->route.final;
            self->redirects++;
            return Success(ChannelCode, CHANNEL_PENDING);
        },
        error(code, message)
        {
            return Error(ChannelCode, message, SPAWN_NO_FILE);
        }
    );
}

static Result(ChannelCode) ExecLoad__step(ExecLoadPtr self, SchedulerPtr scheduler, ProcessPtr process)
{
    ProcessID pid = process->pid;
    BString path = ExecLoad__path(self);
    IORequest request = IORequest__new(self->phase == EXEC_LOAD_OPEN   ? IO_REQUEST_OPEN
                                       : self->phase == EXEC_LOAD_READ ? IO_REQUEST_READ
                                                                       : IO_REQUEST_CLOSE,
                                       self->handle);
    request.client_pid = pid;
    request.buffered = self->phase != EXEC_LOAD_OPEN || self->route_final;
    if (self->phase == EXEC_LOAD_OPEN)
    {
        request.count = BString__copy_to(&path, (char *)request.data, sizeof(request.data));
        request.flags = IO_OPEN_READ | IO_OPEN_EXECUTE;
    }
    else if (self->phase == EXEC_LOAD_READ)
    {
        request.count = BUFFER_SIZE;
        request.buffer = self->transfer.id;
    }
    if (!self->pending)
    {
        match_result(
            KernelChannel__request(&self->channel, scheduler, (ConstChannelDataPtr)&request, sizeof(request)),
            success(code)
            {
                if (code == CHANNEL_PENDING)
                {
                    return Success(ChannelCode, CHANNEL_PENDING);
                }
                self->pending = true;
            },
            error(code, message)
            {
                return Error(ChannelCode, message, SPAWN_FAILED);
            }
        );
    }
    IOReply reply;
    match_result(
        KernelChannel__poll(&self->channel, scheduler, (ChannelDataPtr)&reply, sizeof(reply)),
        success(optional_size)
        {
            match_optional(
                optional_size,
                some(size)
                {
                    if (size != sizeof(reply))
                    {
                        return Error(ChannelCode, load_failed, SPAWN_FAILED);
                    }
                },
                none()
                {
                    return Success(ChannelCode, CHANNEL_PENDING);
                }
            );
        },
        error(code, message)
        {
            return Error(ChannelCode, message, SPAWN_FAILED);
        }
    );
    self->pending = false;
    if (reply.status == IO_REDIRECT && self->phase == EXEC_LOAD_OPEN)
    {
        return ExecLoad__redirect(self, scheduler, process, &reply);
    }
    if (reply.status == IO_WAIT_FOR_DATA)
    {
        return Success(ChannelCode, CHANNEL_PENDING);
    }
    if (reply.status == IO_EOF && self->phase == EXEC_LOAD_READ)
    {
        reply = IOReply__new(IO_OK, 0);
    }
    if (reply.status != IO_OK)
    {
        return Error(ChannelCode, load_failed, self->phase == EXEC_LOAD_OPEN ? SPAWN_NO_FILE : SPAWN_FAILED);
    }
    switch (self->phase)
    {
    case EXEC_LOAD_OPEN:
        if (reply.value < IO_SESSION_HANDLE_BASE)
        {
            return Error(ChannelCode, load_failed, SPAWN_FAILED);
        }
        self->handle = (IOHandle)reply.value;
        if (!BufferDescriptor__valid(&reply.buffers.read, BUFFER_READ))
        {
            return Error(ChannelCode, load_failed, SPAWN_FAILED);
        }
        self->transfer = reply.buffers.read;
        self->phase = EXEC_LOAD_READ;
        break;
    case EXEC_LOAD_READ:
        match_result(
            ExecLoad__append(self, scheduler, &reply),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ChannelCode, message, code);
            }
        );
        if (reply.value == 0)
        {
            self->phase = EXEC_LOAD_CLOSE;
        }
        break;
    case EXEC_LOAD_CLOSE:
        return Success(ChannelCode, CHANNEL_OK);
    }
    return Success(ChannelCode, CHANNEL_PENDING);
}

void ExecLoad__cancel(ProcessPtr process)
{
    match_optional(
        process->exec_load,
        some(load)
        {
            SchedulerPtr scheduler = process->kernel->scheduler;
            critical_section(
                scheduler->scheduler_lock,
                {
                    Buffers__drop(&scheduler->buffers, scheduler, load->transfer.id, QUEUE_KERNEL_PID);
                    KernelChannel__close(&load->channel, scheduler);
                }
            );
            if (load->buffer.valid)
            {
                Kernel__free(load->buffer.virtual_addr);
            }
            if (load->overlay_payload.valid)
            {
                Kernel__free(load->overlay_payload.virtual_addr);
            }
            process->exec_load = None(ExecLoadPtr);
            Kernel__free((Address)load);
        },
        none_do_nothing()
    );
}

Result(ProcessPtr) ExecLoad__continue(ProcessPtr process)
{
    match_optional(
        process->exec_load,
        some(load)
        {
            SchedulerPtr scheduler = process->kernel->scheduler;
            while (true)
            {
                QueueProgress before;
                QueueProgress after;
                ExecLoadPhase phase = load->phase;
                Result(ChannelCode) progress;
                critical_section(
                    scheduler->scheduler_lock,
                    {
                        before = KernelChannel__progress(&load->channel);
                        progress = ExecLoad__step(load, scheduler, process);
                        after = KernelChannel__progress(&load->channel);
                    }
                );
                match_result(
                    progress,
                    success(code)
                    {
                        if (code == CHANNEL_PENDING)
                        {
                            if (phase != load->phase || !QueueProgress__equals(&before, &after))
                            {
                                continue;
                            }
                            load->observation = after;
                            load->observation_valid = true;
                            return Error(ProcessPtr, bstr("Executable load pending"), SPAWN_PENDING);
                        }
                    },
                    error(code, message)
                    {
                        ExecLoad__cancel(process);
                        return Error(ProcessPtr, message, code);
                    }
                );
                break;
            }
            critical_section(
                scheduler->scheduler_lock,
                {
                    Buffers__drop(&scheduler->buffers, scheduler, load->transfer.id, QUEUE_KERNEL_PID);
                    KernelChannel__close(&load->channel, scheduler);
                }
            );
            if (!load->buffer.valid)
            {
                ExecLoad__dispose_finished(process, load);
                return Error(ProcessPtr, load_failed, SPAWN_FAILED);
            }
            BString path = ExecLoad__requested_path(load);
            Elf elf = Elf__new(load->buffer.virtual_addr, load->buffer.virtual_addr + load->size, &path);
            if (!load->loading_interpreter)
            {
                match_result(
                    Elf__image_kind(&elf),
                    success(kind)
                    {
                        if (kind == ELF_IMAGE_DYNAMIC)
                        {
                            Result(ChannelCode) interpreter;
                            critical_section(
                                scheduler->scheduler_lock,
                                {
                                    interpreter = ExecLoad__begin_interpreter(load, scheduler, process);
                                }
                            );
                            match_result(
                                interpreter,
                                success(code)
                                {
                                    return Error(ProcessPtr, bstr("Executable load pending"), SPAWN_PENDING);
                                },
                                error(code, message)
                                {
                                    ExecLoad__dispose_finished(process, load);
                                    return Error(ProcessPtr, message, code);
                                }
                            );
                        }
                    },
                    error(code, message)
                    {
                        ExecLoad__dispose_finished(process, load);
                        return Error(ProcessPtr, message, SPAWN_FAILED);
                    }
                );
            }
            process->exec_load = None(ExecLoadPtr);
            KernelAddress overlay_payload = load->overlay_payload;
            Result(ProcessPtr) result =
                Exec__launch_loaded_elf(&load->context, &elf, load->wait, &load->user, &load->arguments, &load->stdio,
                                        &load->abort_report, load->overlay, overlay_payload);
            if (overlay_payload.valid)
            {
                Kernel__free(overlay_payload.virtual_addr);
            }
            Kernel__free((Address)load);
            return result;
        },
        none()
        {
            return Error(ProcessPtr, load_failed, SPAWN_FAILED);
        }
    );
}

bool ExecLoad__ready(ProcessPtr process)
{
    match_optional(
        process->exec_load,
        some(load)
        {
            if (!load->observation_valid)
            {
                return true;
            }
            QueueProgress current = KernelChannel__progress(&load->channel);
            return !QueueProgress__equals(&load->observation, &current);
        },
        none_do_nothing()
    );
    return false;
}

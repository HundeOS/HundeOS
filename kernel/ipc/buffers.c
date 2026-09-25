#include "buffers.h"

bool Buffers__has_pending_notifications(BuffersPtr self, ProcessPtr process)
{
    return self->manager == process->pid && self->notifications.valid &&
           KernelNotificationQueue__has_pending(k_v_ptr(self->notifications, KernelNotificationQueue));
}

#include "../scheduler/scheduler.h"
#include "../kernel_core.h"
#include "ipc/ipc_platform.h"

FixedArrayBodyDefine(ManagedBuffers);
static BString buffer_error = bstr("Shared buffer unavailable");

static ManagedBuffer ManagedBuffer__new(ArrayIndex slot)
{
    return (ManagedBuffer){.id = slot};
}

bool ManagedBuffer__equals(void * first, void * second)
{
    return ((ManagedBufferPtr)first)->id == ((ManagedBufferPtr)second)->id;
}

static Optional(ManagedBufferPtr) Buffers__find(BuffersPtr self, BufferID id)
{
    match_optional(
        ManagedBuffers__ptr_at(&self->allocations, id % BUFFER_LIMIT),
        some(buffer)
        {
            if (id >= BUFFER_LIMIT && buffer->id == id && buffer->memory.valid)
            {
                return Some(ManagedBufferPtr, buffer);
            }
        },
        none_do_nothing()
    );
    return None(ManagedBufferPtr);
}

static void Buffers__notify(BuffersPtr self, SchedulerPtr scheduler, ManagedBufferPtr buffer)
{
    if (self->notifications.valid)
    {
        KernelNotificationQueue__publish(k_v_ptr(self->notifications, KernelNotificationQueue),
                                         buffer->id % BUFFER_LIMIT, buffer->id);
        match_optional(
            Scheduler__find_process_by_pid_locked(scheduler, self->manager),
            some(manager)
            {
                Process__wake_shared_wait(manager);
            },
            none_do_nothing()
        );
        IpcPlatform__signal_event();
    }
}

static Result(Address) Buffers__register(BuffersPtr self, SchedulerPtr scheduler, ProcessPtr caller)
{
    if (!BString__equals(&caller->name, &bstr("/sbin/services/hunde-puffer")) ||
        !ProcessClass__is_service(caller->process_class) ||
        !Process__has_capability(caller, PROCESS_CAP_MANAGE_SHARED_PAGES) ||
        (self->manager != 0 && self->manager != caller->pid))
    {
        return Error(Address, buffer_error, BUFFER_DENIED);
    }
    if (self->manager == caller->pid)
    {
        return Success(Address, BUFFER_EVENTS_ADDRESS);
    }
    if (!self->notifications.valid)
    {
        match_result(
            Kernel__try_alloc_user(BUFFER_EVENTS_ADDRESS, PAGE_SIZE, true, true, false),
            success(memory)
            {
                self->notifications = memory;
            },
            error(code, message)
            {
                return Error(Address, message, BUFFER_NO_MEMORY);
            }
        );
    }
    Kernel__memset(self->notifications.kernel_addr.virtual_addr, 0, PAGE_SIZE);
    match_result(
        PageTable__try_map_shared(&caller->page_table, caller->kernel, self->notifications, PAGE_SIZE),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Address, message, BUFFER_NO_MEMORY);
        }
    );
    self->manager = caller->pid;
    for_each_fa(
        ManagedBuffers,
        self->allocations,
        index,
        buffer,
        {
            if (buffer.memory.valid)
            {
                Buffers__notify(self, scheduler, &buffer);
            }
        }
    );
    return Success(Address, BUFFER_EVENTS_ADDRESS);
}

static Result(BufferStatus)
    Buffers__map(ManagedBufferPtr self, SchedulerPtr scheduler, BufferParticipant pid, bool write)
{
    if (pid == QUEUE_KERNEL_PID)
    {
        return Success(BufferStatus, BUFFER_OK);
    }
    match_optional(
        Scheduler__find_process_by_pid_locked(scheduler, pid),
        some(process)
        {
            UserAddress mapping = self->memory;
            mapping.write = write;
            match_result(
                PageTable__try_map_shared(&process->page_table, process->kernel, mapping, BUFFER_SIZE),
                success(code)
                {
                    return Success(BufferStatus, BUFFER_OK);
                },
                error(code, message)
                {
                    return Error(BufferStatus, message, BUFFER_NO_MEMORY);
                }
            );
        },
        none()
        {
            return Error(BufferStatus, buffer_error, BUFFER_UNAVAILABLE);
        }
    );
}

static void Buffers__unmap(ManagedBufferPtr self, SchedulerPtr scheduler, BufferParticipant pid)
{
    if (pid == 0 || pid == QUEUE_KERNEL_PID)
    {
        return;
    }
    match_optional(
        Scheduler__find_process_by_pid_locked(scheduler, pid),
        some(process)
        {
            PageTable__unmap(&process->page_table, process->kernel, self->memory.virtual_addr, BUFFER_SIZE);
        },
        none_do_nothing()
    );
}

static Result(Address)
    Buffers__activate(BuffersPtr self, SchedulerPtr scheduler, ManagedBufferPtr buffer, ArrayIndex slot,
                      BufferParticipant server, BufferParticipant client, BufferDirection direction)
{
    if (self->generation >= INT64_MAX / BUFFER_LIMIT - 1)
    {
        return Error(Address, buffer_error, BUFFER_NO_MEMORY);
    }
    Kernel__memset(buffer->memory.kernel_addr.virtual_addr, 0, BUFFER_SIZE);
    match_result(
        Buffers__map(buffer, scheduler, server, direction == BUFFER_READ),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );
    match_result(
        Buffers__map(buffer, scheduler, client, direction == BUFFER_WRITE),
        success_do_nothing(),
        error(code, message)
        {
            Buffers__unmap(buffer, scheduler, server);
            return Error(Address, message, code);
        }
    );
    buffer->client = client;
    buffer->server = server;
    buffer->direction = direction;
    buffer->retiring = false;
    match_optional(
        Scheduler__find_process_by_pid_locked(scheduler, client),
        some(process)
        {
            buffer->retained = ProcessClass__is_service(process->process_class);
        },
        none()
        {
            buffer->retained = false;
        }
    );
    buffer->id = (++self->generation) * BUFFER_LIMIT + slot;
    return Success(Address, buffer->id);
}

static bool ManagedBuffer__unused(ManagedBufferPtr self, void * context)
{
    return !self->memory.valid;
}

static Result(Address) Buffers__create(BuffersPtr self, SchedulerPtr scheduler, BufferParticipant server,
                                       BufferParticipant client, BufferDirection direction, BufferID reuse)
{
    if (self->generation >= INT64_MAX / BUFFER_LIMIT - 2)
    {
        return Error(Address, buffer_error, BUFFER_NO_MEMORY);
    }

    if (client == 0 || server == 0 || server == client || server == QUEUE_KERNEL_PID ||
        (direction != BUFFER_READ && direction != BUFFER_WRITE))
    {
        return Error(Address, buffer_error, BUFFER_INVALID);
    }

    match_optional(
        Scheduler__find_process_by_pid_locked(scheduler, server),
        some(process)
        {
            if (!ProcessClass__is_service(process->process_class) ||
                !Process__has_capability(process, PROCESS_CAP_MANAGE_SHARED_PAGES))
            {
                return Error(Address, buffer_error, BUFFER_DENIED);
            }
        },
        none()
        {
            return Error(Address, buffer_error, BUFFER_UNAVAILABLE);
        }
    );
    if (client != QUEUE_KERNEL_PID && is_nothing(Scheduler__find_process_by_pid_locked(scheduler, client)))
    {
        return Error(Address, buffer_error, BUFFER_UNAVAILABLE);
    }
    ArrayIndex slot = 0;
    if (reuse != 0)
    {
        match_optional(
            Buffers__find(self, reuse),
            some(buffer)
            {
                if (buffer->retained || buffer->client != 0 || buffer->server != 0)
                {
                    return Error(Address, buffer_error, BUFFER_BUSY);
                }
                return Buffers__activate(self, scheduler, buffer, reuse % BUFFER_LIMIT, server, client, direction);
            },
            none()
            {
                return Error(Address, buffer_error, BUFFER_INVALID);
            }
        );
    }
    match_find_result(
        ManagedBuffers__find(&self->allocations, ManagedBuffer__unused, self),
        some(found)
        {
            slot = found.id % BUFFER_LIMIT;
        },
        none()
        {
            slot = ManagedBuffers__len(&self->allocations);
            match_result(
                ManagedBuffers__push(&self->allocations, ManagedBuffer__new(slot)),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(Address, message, BUFFER_NO_MEMORY);
                }
            );
        }
    );
    match_optional(
        ManagedBuffers__ptr_at(&self->allocations, slot),
        some(buffer)
        {
            match_result(
                Kernel__try_alloc_user(BUFFER_BASE + slot * BUFFER_SIZE, BUFFER_SIZE, true, false, false),
                success(memory)
                {
                    buffer->memory = memory;
                },
                error(code, message)
                {
                    return Error(Address, message, BUFFER_NO_MEMORY);
                }
            );
            // Even failed setup retains an identifiable, reclaimable allocation.
            buffer->id = (++self->generation) * BUFFER_LIMIT + slot;
            match_result(
                Buffers__activate(self, scheduler, buffer, slot, server, client, direction),
                success(id)
                {
                    return Success(Address, id);
                },
                error(code, message)
                {
                    Buffers__notify(self, scheduler, buffer);
                    return Error(Address, message, code);
                }
            );
        },
        none()
        {
            return Error(Address, buffer_error, BUFFER_NO_MEMORY);
        }
    );
}

Result(BufferStatus) Buffers__drop(BuffersPtr self, SchedulerPtr scheduler, BufferID id, BufferParticipant participant)
{
    match_optional(
        Buffers__find(self, id),
        some(buffer)
        {
            if (participant == 0 || (buffer->client != participant && buffer->server != participant))
            {
                return Error(BufferStatus, buffer_error, BUFFER_DENIED);
            }
            Buffers__unmap(buffer, scheduler, participant);
            if (buffer->client == participant)
            {
                buffer->client = 0;
            }
            if (buffer->server == participant)
            {
                buffer->server = 0;
            }
            buffer->retiring = true;
            Buffers__notify(self, scheduler, buffer);
            return Success(BufferStatus, BUFFER_OK);
        },
        none()
        {
            return Error(BufferStatus, buffer_error, BUFFER_INVALID);
        }
    );
}

static Result(Address) ManagedBuffer__device_address(ManagedBufferPtr self, ProcessPtr caller, uint64_t offset,
                                                     uint64_t size, BufferDirection direction)
{
    if (!self->retained || self->retiring || self->server != caller->pid || self->direction != direction ||
        !Process__has_capability(caller, PROCESS_CAP_MANAGE_SHARED_PAGES))
    {
        return Error(Address, buffer_error, BUFFER_DENIED);
    }

    if (size == 0 || offset >= BUFFER_SIZE || size > BUFFER_SIZE - offset)
    {
        return Error(Address, buffer_error, BUFFER_INVALID);
    }

    Address start = self->memory.kernel_addr.virtual_addr + offset;
    KernelAddress first = Kernel__from_virtual_address(caller->kernel, start);
    if (!first.valid)
    {
        return Error(Address, buffer_error, BUFFER_INVALID);
    }
    // The current device API uses physical addresses. Verify the entire segment,
    // not just its first page; retained backing cannot be recycled during DMA.
    for (uint64_t step = PAGE_SIZE - start % PAGE_SIZE; step < size; step += PAGE_SIZE)
    {
        KernelAddress page = Kernel__from_virtual_address(caller->kernel, start + step);
        if (!page.valid || page.phys_addr != first.phys_addr + step)
        {
            return Error(Address, buffer_error, BUFFER_INVALID);
        }
    }
    return Success(Address, first.phys_addr);
}

Result(Address) Buffers__operation(BuffersPtr self, SchedulerPtr scheduler, ProcessPtr caller, uint64_t operation,
                                   uint64_t first, uint64_t second, uint64_t third, uint64_t fourth)
{
    if (operation == MEM_BUFFER_REGISTER)
    {
        return Buffers__register(self, scheduler, caller);
    }
    if (operation == MEM_BUFFER_DEVICE_ADDRESS)
    {
        match_optional(
            Buffers__find(self, first),
            some(buffer)
            {
                return ManagedBuffer__device_address(buffer, caller, second, third, fourth);
            },
            none()
            {
                return Error(Address, buffer_error, BUFFER_DENIED);
            }
        );
    }

    if (operation == MEM_BUFFER_VALIDATE)
    {
        match_optional(
            Buffers__find(self, first),
            some(buffer)
            {
                if ((buffer->client == caller->pid || buffer->server == caller->pid) && !buffer->retiring &&
                    buffer->direction == second && (buffer->server == third || buffer->client == third))
                {
                    return Success(Address, buffer->memory.virtual_addr);
                }
            },
            none_do_nothing()
        );
        return Error(Address, buffer_error, BUFFER_DENIED);
    }
    if (caller->pid != self->manager || self->manager == 0)
    {
        return Error(Address, buffer_error, BUFFER_DENIED);
    }
    if (operation == MEM_BUFFER_CREATE)
    {
        return Buffers__create(self, scheduler, first, second, third, fourth);
    }
    if (operation == MEM_BUFFER_AT)
    {
        match_optional(
            ManagedBuffers__at(&self->allocations, first),
            some(buffer)
            {
                return Success(Address, buffer.memory.valid ? buffer.id : 0);
            },
            none()
            {
                return Success(Address, 0);
            }
        );
    }
    if (operation == MEM_BUFFER_DROP)
    {
        match_result(
            Buffers__drop(self, scheduler, first, second),
            success(code)
            {
                return Success(Address, code);
            },
            error(code, message)
            {
                return Error(Address, message, code);
            }
        );
    }
    match_optional(
        Buffers__find(self, first),
        some(buffer)
        {
            if (operation == MEM_BUFFER_STATE)
            {
                return Success(Address, (buffer->client != 0 ? BUFFER_CLIENT_ATTACHED : 0) |
                                            (buffer->server != 0 ? BUFFER_SERVER_ATTACHED : 0) |
                                            (buffer->retiring ? BUFFER_RETIRING : 0) |
                                            (buffer->retained ? BUFFER_RETAINED : 0));
            }
            if (operation == MEM_BUFFER_FREE && !buffer->retained && buffer->client == 0 && buffer->server == 0)
            {
                match_result(
                    Kernel__free(buffer->memory.kernel_addr.virtual_addr),
                    success(code)
                    {
                        buffer->memory = NULL_USER_ADDRESS;
                        return Success(Address, BUFFER_OK);
                    },
                    error(code, message)
                    {
                        return Error(Address, message, BUFFER_NO_MEMORY);
                    }
                );
            }
        },
        none_do_nothing()
    );
    return Error(Address, buffer_error, BUFFER_BUSY);
}

void Buffers__exit(BuffersPtr self, SchedulerPtr scheduler, ProcessPtr process)
{
    // Device quiescence precedes scheduler detachment. Other participants retain their backing.
    for_each_fa(
        ManagedBuffers,
        self->allocations,
        index,
        value,
        {
            match_optional(
                ManagedBuffers__ptr_at(&self->allocations, index),
                some(buffer)
                {
                    if (buffer->memory.valid &&
                        (buffer->client == process->pid || buffer->server == process->pid))
                    {
                        PageTable__unmap(&process->page_table, process->kernel, buffer->memory.virtual_addr,
                                         BUFFER_SIZE);
                        if (buffer->client == process->pid)
                        {
                            buffer->client = 0;
                        }
                        if (buffer->server == process->pid)
                        {
                            buffer->server = 0;
                        }
                        buffer->retiring = true;
                        Buffers__notify(self, scheduler, buffer);
                    }
                },
                none_do_nothing()
            );
        }
    );
    if (self->manager == process->pid)
    {
        PageTable__unmap(&process->page_table, process->kernel, BUFFER_EVENTS_ADDRESS, PAGE_SIZE);
        self->manager = 0;
    }
}

Result(Address) Buffers__kernel_read(BuffersPtr self, BufferID id, BufferParticipant server, uint64_t size)
{
    match_optional(
        Buffers__find(self, id),
        some(buffer)
        {
            if (buffer->client == QUEUE_KERNEL_PID && buffer->server == server && buffer->direction == BUFFER_READ &&
                !buffer->retiring && size <= BUFFER_SIZE)
            {
                return Success(Address, buffer->memory.kernel_addr.virtual_addr);
            }
        },
        none_do_nothing()
    );
    return Error(Address, buffer_error, BUFFER_DENIED);
}

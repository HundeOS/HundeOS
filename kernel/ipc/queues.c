#include "queues.h"
#include "../arch/page_table.h"
#include "../kernel_core.h"

FixedArrayBodyDefine(QueueListeners);
FixedArrayBodyDefine(QueueConnections);

static BString queue_error = bstr("Atomic message queue operation failed");

bool QueueListener__equals(void * first, void * second)
{
    QueueListenerPtr a = first;
    QueueListenerPtr b = second;
    return a->endpoint == b->endpoint;
}

bool QueueConnection__equals(void * first, void * second)
{
    QueueConnectionPtr a = first;
    QueueConnectionPtr b = second;
    return a->connection == b->connection;
}

static QueueListener QueueListener__new(UserAddress arrivals, OptionalPtr(ProcessPtr) process, QueueEndpointID endpoint)
{
    return (QueueListener){.arrivals = arrivals, .process = *process, .endpoint = endpoint};
}

static QueueConnection QueueConnection__new(UserAddress memory, OptionalPtr(ProcessPtr) client,
                                            OptionalPtr(ProcessPtr) server, QueueConnectionID connection,
                                            QueueSlotIndex listener_index)
{
    return (QueueConnection){
        .memory = memory,
        .client = *client,
        .server = *server,
        .kernel_origin = None(ProcessPtr),
        .connection = connection,
        .listener_index = listener_index,
    };
}

static bool Queues__belongs_to(OptionalPtr(ProcessPtr) owner, ProcessPtr process)
{
    match_optional(*owner,
        some(value)
        {
            return value == process;
        },
        none_do_nothing()
    );
    return false;
}

static bool QueueConnection__has_unread_messages(QueueConnectionPtr self, void * context)
{
    ProcessPtr process = context;
    bool client = Queues__belongs_to(&self->client, process);
    if (!self->memory.valid || (!client && !Queues__belongs_to(&self->server, process)))
    {
        return false;
    }
    Address base = self->memory.kernel_addr.virtual_addr;
    Queue queue = Queue__new((QueueIdentityPtr)base, (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE),
                             (QueuePagePtr)(base + (client ? 2 : 1) * QUEUE_PAGE_SIZE), self->connection, 0);
    return Queue__has_unread_messages(&queue);
}

bool Queues__has_unread_messages(QueuesPtr self, ProcessPtr process)
{
    match_find_result(
        QueueConnections__find(&self->connections, QueueConnection__has_unread_messages, process),
        some(connection)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

Result(QueueCode) Queues__watch(QueuesPtr self, ProcessPtr process, ConstQueueWaitSnapshotPtr snapshot)
{
    if (snapshot->kind == QUEUE_WAIT_ARRIVALS)
    {
        match_optional(
            QueueListeners__ptr_at(&self->listeners, snapshot->handle % QUEUE_LISTENER_LIMIT),
            some(listener)
            {
                if (listener->endpoint == snapshot->handle && Queues__belongs_to(&listener->process, process))
                {
                    listener->wait = Some(QueueWaitSnapshot, *snapshot);
                    return Success(QueueCode, QUEUE_OK);
                }
            },
            none_do_nothing()
        );
    }
    if (snapshot->kind == QUEUE_WAIT_CONNECTION)
    {
        match_optional(
            QueueConnections__ptr_at(&self->connections, snapshot->handle % QUEUE_LIMIT),
            some(connection)
            {
                if (connection->connection == snapshot->handle)
                {
                    if (Queues__belongs_to(&connection->client, process))
                    {
                        connection->client_wait = Some(QueueWaitSnapshot, *snapshot);
                        return Success(QueueCode, QUEUE_OK);
                    }
                    if (Queues__belongs_to(&connection->server, process))
                    {
                        connection->server_wait = Some(QueueWaitSnapshot, *snapshot);
                        return Success(QueueCode, QUEUE_OK);
                    }
                }
            },
            none_do_nothing()
        );
    }
    return Error(QueueCode, queue_error, QUEUE_INVALID);
}

static bool QueueConnection__wait_changed(QueueConnectionPtr self, bool client)
{
    match_optional(
        client ? self->client_wait : self->server_wait,
        some(observed)
        {
            Address base = self->memory.kernel_addr.virtual_addr;
            Queue queue = Queue__new((QueueIdentityPtr)base, (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE),
                                     (QueuePagePtr)(base + (client ? 2 : 1) * QUEUE_PAGE_SIZE), self->connection, 0);
            QueueWaitSnapshot current = QueueWaitSnapshot__new_connection(&queue);
            return !QueueWaitSnapshot__equals(&observed, &current);
        },
        none_do_nothing()
    );
    return false;
}

bool Queues__observation_changed(QueuesPtr self, ProcessPtr process, ConstQueueWaitSnapshotPtr observed)
{
    if (observed->kind == QUEUE_WAIT_ARRIVALS)
    {
        match_optional(
            QueueListeners__ptr_at(&self->listeners, observed->handle % QUEUE_LISTENER_LIMIT),
            some(listener)
            {
                if (listener->arrivals.valid && listener->endpoint == observed->handle &&
                    Queues__belongs_to(&listener->process, process))
                {
                    QueueWaitSnapshot current =
                        QueueWaitSnapshot__new_arrivals(k_v_ptr(listener->arrivals, QueueArrivals));
                    return !QueueWaitSnapshot__equals(observed, &current);
                }
            },
            none_do_nothing()
        );
    }
    if (observed->kind == QUEUE_WAIT_CONNECTION)
    {
        match_optional(
            QueueConnections__ptr_at(&self->connections, observed->handle % QUEUE_LIMIT),
            some(connection)
            {
                bool client = Queues__belongs_to(&connection->client, process);
                if (connection->memory.valid && connection->connection == observed->handle &&
                    (client || Queues__belongs_to(&connection->server, process)))
                {
                    Address base = connection->memory.kernel_addr.virtual_addr;
                    Queue queue = Queue__new(
                        (QueueIdentityPtr)base, (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE),
                        (QueuePagePtr)(base + (client ? 2 : 1) * QUEUE_PAGE_SIZE), connection->connection, 0);
                    QueueWaitSnapshot current = QueueWaitSnapshot__new_connection(&queue);
                    return !QueueWaitSnapshot__equals(observed, &current);
                }
            },
            none_do_nothing()
        );
    }
    // Invalid, detached, or foreign observations must never suppress scheduling.
    return true;
}

bool Queues__wait_changed(QueuesPtr self, ProcessPtr process)
{
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        listener,
        {
            if (Queues__belongs_to(&listener.process, process))
            {
                match_optional(
                    listener.wait,
                    some(observed)
                    {
                        QueueWaitSnapshot current =
                            QueueWaitSnapshot__new_arrivals(k_v_ptr(listener.arrivals, QueueArrivals));
                        if (!QueueWaitSnapshot__equals(&observed, &current))
                        {
                            return true;
                        }
                    },
                    none_do_nothing()
                );
            }
        }
    );
    for_each_fa(
        QueueConnections,
        self->connections,
        index,
        connection,
        {
            if ((Queues__belongs_to(&connection.client, process) && QueueConnection__wait_changed(&connection, true)) ||
                (Queues__belongs_to(&connection.server, process) && QueueConnection__wait_changed(&connection, false)))
            {
                return true;
            }
        }
    );
    return false;
}

void Queues__clear_wait(QueuesPtr self, ProcessPtr process)
{
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        listener,
        {
            if (Queues__belongs_to(&listener.process, process))
            {
                match_optional(
                    QueueListeners__ptr_at(&self->listeners, index),
                    some(stored)
                    {
                        stored->wait = None(QueueWaitSnapshot);
                    },
                    none_do_nothing()
                );
            }
        }
    );
    for_each_fa(
        QueueConnections,
        self->connections,
        index,
        connection,
        {
            match_optional(
                QueueConnections__ptr_at(&self->connections, index),
                some(stored)
                {
                    if (Queues__belongs_to(&stored->client, process))
                    {
                        stored->client_wait = None(QueueWaitSnapshot);
                    }
                    if (Queues__belongs_to(&stored->server, process))
                    {
                        stored->server_wait = None(QueueWaitSnapshot);
                    }
                },
                none_do_nothing()
            );
        }
    );
}

static bool Queues__range_available(ProcessPtr process, Address address, size_t size)
{
    for_each_ll(
        UserAllocations,
        process->user_allocations,
        allocation,
        {
            Address start = allocation.address.virtual_addr;
if (start <= address ? address - start < allocation.size : start - address < size)
{
return false;
}
        }
    );
    return true;
}

static void QueueListener__notify(QueueListenerPtr self)
{
    QueueArrivalsPtr arrivals = k_v_ptr(self->arrivals, QueueArrivals);
    __atomic_add_fetch(&arrivals->epoch, 1, __ATOMIC_RELEASE);
    IpcPlatform__signal_event();
}

static Result(QueueHandleID) Queues__next_id(QueuesPtr self, QueueSlotIndex index)
{
    if (self->generation >= (INT64_MAX / QUEUE_LIMIT) - 1)
    {
        return Error(QueueHandleID, queue_error, QUEUE_FULL);
    }
    self->generation++;
    return Success(QueueHandleID, self->generation * QUEUE_LIMIT + index);
}

static void QueueConnection__map(QueueConnectionPtr self, ProcessPtr process, bool client)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        UserAddress page = UserAddress__relative(&self->memory, index * QUEUE_PAGE_SIZE);
        page.write = index == (client ? 1U : 2U);
        PageTable__map_shared(&process->page_table, process->kernel, page, QUEUE_PAGE_SIZE);
    }
}

static void QueueConnection__unmap(QueueConnectionPtr self, ProcessPtr process)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        PageTable__unmap(&process->page_table, process->kernel, self->memory.virtual_addr + index * QUEUE_PAGE_SIZE,
                         QUEUE_PAGE_SIZE);
    }
}

static Result(Address) QueueConnection__release(QueueConnectionPtr self)
{
    if (self->memory.valid && !self->kernel_client && is_nothing(self->client) && is_nothing(self->server))
    {
        match_result(
            Kernel__free(self->memory.kernel_addr.virtual_addr),
            success(_)
            {
                *self = QueueConnection__new(NULL_USER_ADDRESS, &None(ProcessPtr), &None(ProcessPtr), 0, 0);
            },
            error(code, message)
            {
                return Error(Address, message, QUEUE_NO_MEMORY);
            }
        );
    }

    return Success(Address, QUEUE_OK);
}

static void QueueListener__release(QueueListenerPtr self)
{
    if (!self->arrivals.valid || !is_nothing(self->process))
    {
        return;
    }
    match_result(
        Kernel__free(self->arrivals.kernel_addr.virtual_addr),
        success(_)
        {
            *self = QueueListener__new(NULL_USER_ADDRESS, &None(ProcessPtr), 0);
        },
        error(code, message)
        {
            __d_printf(MEMORY, "Queue listener backing quarantined: %i\n", code);
        }
    );
}

Result(Address) Queues__listen(QueuesPtr self, ProcessPtr process, ConstBStringPtr name)
{
    if (!ProcessClass__is_service(process->process_class) ||
        !Process__has_capability(process, PROCESS_CAP_BECOME_SERVICE))
    {
        return Error(Address, queue_error, QUEUE_DENIED);
    }
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        listener,
        {
            if (Queues__belongs_to(&listener.process, process))
            {
                return Error(Address, queue_error, QUEUE_OCCUPIED);
            }
            match_optional(
                QueueListeners__ptr_at(&self->listeners, index),
                some(slot)
                {
                    QueueListener__release(slot);
                },
                none_do_nothing()
            );
        }
    );

    uint64_t selected = QueueListeners__len(&self->listeners);
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        listener,
        {
            if (!listener.arrivals.valid)
            {
                selected = index;
                break;
            }
        }
    );
    Address address = QUEUE_LISTENER_BASE + selected * QUEUE_ARRIVALS_SIZE;
    if (selected >= QUEUE_LISTENER_LIMIT || !Queues__range_available(process, address, QUEUE_ARRIVALS_SIZE))
    {
        return Error(Address, queue_error, QUEUE_FULL);
    }
    QueueEndpointID endpoint;
    match_result(
        Queues__next_id(self, selected),
        success(value)
        {
            endpoint = value;
        },
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );
    match_result(
        Kernel__try_alloc_user(address, QUEUE_ARRIVALS_SIZE, true, false, false),
        success(memory)
        {
            Kernel__memset(memory.kernel_addr.virtual_addr, 0, QUEUE_ARRIVALS_SIZE);
            QueueArrivalsPtr arrivals = k_v_ptr(memory, QueueArrivals);
            arrivals->endpoint = endpoint;
            QueueListeners__set(&self->listeners, selected, QueueListener__new(memory, &None(ProcessPtr), endpoint));
            match_result(
                KernelServiceDirectory__publish_endpoint(&process->kernel->service_directory, process, name, endpoint),
                success_do_nothing(),
                error(code, message)
                {
                    match_optional(
                        QueueListeners__ptr_at(&self->listeners, selected),
                        some(slot)
                        {
                            QueueListener__release(slot);
                        },
                        none_do_nothing()
                    );
                    return Error(Address, message, code);
                }
            );
            QueueListeners__set(&self->listeners, selected,
                                QueueListener__new(memory, &Some(ProcessPtr, process), endpoint));
            PageTable__map_shared(&process->page_table, process->kernel, memory, QUEUE_ARRIVALS_SIZE);
            return Success(Address, address);
        },
        error(code, message)
        {
            return Error(Address, message, QUEUE_NO_MEMORY);
        }
    );
    return Error(Address, queue_error, QUEUE_NO_MEMORY);
}

static Result(QueueConnectionID) Queues__connect_to(QueuesPtr self, QueueListenerPtr listener, ProcessPtr server,
                                                    OptionalPtr(ProcessPtr) client, QueueSlotIndex listener_index)
{
    QueueParticipantID client_pid = QUEUE_KERNEL_PID;
    uint64_t client_limit = QUEUE_LIMIT;
    match_optional(
        *client,
        some(process)
        {
            if (process == server)
            {
                return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
            }
            client_pid = process->pid;
            client_limit = ProcessClass__is_service(process->process_class) &&
                                   Process__has_capability(process, PROCESS_CAP_MANAGE_SHARED_PAGES)
                               ? QUEUE_LIMIT
                               : 4;
        },
        none_do_nothing()
    );
    uint64_t selected = QueueConnections__len(&self->connections);
    uint64_t client_count = 0;
    for_each_fa(
        QueueConnections,
        self->connections,
        index,
        connection,
        {
            match_optional(
                *client,
                some(process)
                {
                    if (Queues__belongs_to(&connection.client, process))
                    {
                        client_count++;
                    }
                },
                none_do_nothing()
            );
            if (!connection.memory.valid)
            {
                selected = index;
            }
        }
    );
    if (selected >= QUEUE_LIMIT || client_count >= client_limit)
    {
        return Error(QueueConnectionID, queue_error, QUEUE_FULL);
    }

    Address address = QueueLayout__address(selected);
    match_optional(
        *client,
        some(process)
        {
            if (!Queues__range_available(process, address, QUEUE_CONNECTION_SIZE))
            {
                return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
            }
        },
        none_do_nothing()
    );
    if (!Queues__range_available(server, address, QUEUE_CONNECTION_SIZE))
    {
        return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
    }
    QueueConnectionID id;
    match_result(
        Queues__next_id(self, selected),
        success(value)
        {
            id = value;
        },
        error(code, message)
        {
            return Error(QueueConnectionID, message, code);
        }
    );
    match_result(
        Kernel__try_alloc_user(address, QUEUE_CONNECTION_SIZE, true, false, false),
        success(memory)
        {
            Kernel__memset(memory.kernel_addr.virtual_addr, 0, QUEUE_CONNECTION_SIZE);
            QueueIdentityPtr identity = k_v_ptr(memory, QueueIdentity);
            identity->connection = id;
            identity->endpoint = listener->endpoint;
            identity->client_pid = client_pid;
            identity->server_pid = server->pid;
            identity->origin_pid = client_pid;
            QueueConnection connection =
                QueueConnection__new(memory, client, &Some(ProcessPtr, server), id, listener_index);
            connection.kernel_client = is_nothing((*client));
            QueueConnections__set(&self->connections, selected, connection);
            match_optional(
                *client,
                some(process)
                {
                    QueueConnection__map(&connection, process, true);
                },
                none_do_nothing()
            );
            QueueConnection__map(&connection, server, false);
            QueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, QueueArrivals);
            match_result(
                QueueConnectionSlots__set(&arrivals->connections, selected, id),
                success_do_nothing(),
                error(code, message)
                {
                    __builtin_trap();
                }
            );
            QueueListener__notify(listener);
            return Success(QueueConnectionID, id);
        },
        error(code, message)
        {
            return Error(QueueConnectionID, message, QUEUE_NO_MEMORY);
        }
    );
    return Error(QueueConnectionID, queue_error, QUEUE_NO_MEMORY);
}

Result(QueueConnectionID) Queues__connect(QueuesPtr self, ProcessPtr process, QueueEndpointID endpoint)
{
    // Failed frees remain quarantined and are retried before allocating new slots.
    for_each_fa(
        QueueConnections,
        self->connections,
        index,
        value,
        {
            match_optional(
                QueueConnections__ptr_at(&self->connections, index),
                some(connection)
                {
                    QueueConnection__release(connection);
                },
                none_do_nothing()
            );
        }
    );
    match_optional(
        QueueListeners__ptr_at(&self->listeners, endpoint % QUEUE_LIMIT),
        some(listener)
        {
            if (!listener->arrivals.valid || listener->endpoint != endpoint)
            {
                return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
            }
            match_optional(
                listener->process,
                some(server)
                {
                    return Queues__connect_to(self, listener, server, &Some(ProcessPtr, process),
                                              endpoint % QUEUE_LIMIT);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
}

Result(QueueCode) Queues__accept(QueuesPtr self, ProcessPtr process, QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&self->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid &&
                Queues__belongs_to(&connection->server, process))
            {
                QueueIdentityPtr identity = k_v_ptr(connection->memory, QueueIdentity);
                if (__atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0)
                {
                    return Error(QueueCode, queue_error, QUEUE_CLOSED);
                }
                if (__atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) != 0)
                {
                    return Error(QueueCode, queue_error, QUEUE_OCCUPIED);
                }
                __atomic_store_n(&identity->accepted, 1, __ATOMIC_RELEASE);
                IpcPlatform__signal_event();
                return Success(QueueCode, QUEUE_OK);
            }
        },
        none_do_nothing()
    );
    return Error(QueueCode, queue_error, QUEUE_DENIED);
}

static Result(QueueCode) Queues__close_connection(QueuesPtr self, QueueConnectionPtr connection, ProcessPtr process)
{
    bool client = Queues__belongs_to(&connection->client, process);
    bool server = Queues__belongs_to(&connection->server, process);
    if (!client && !server)
    {
        return Error(QueueCode, queue_error, QUEUE_DENIED);
    }
    QueueIdentityPtr identity = k_v_ptr(connection->memory, QueueIdentity);
    __atomic_store_n(&identity->closed, 1, __ATOMIC_RELEASE);
    QueueConnection__unmap(connection, process);
    if (client)
    {
        connection->client = None(ProcessPtr);
    }
    if (server)
    {
        connection->server = None(ProcessPtr);
    }
    match_optional(
        QueueListeners__ptr_at(&self->listeners, connection->listener_index),
        some(listener)
        {
            if (listener->arrivals.valid && listener->endpoint == identity->endpoint)
            {
                QueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, QueueArrivals);
                if (server)
                {
                    match_result(
                        QueueConnectionSlots__set(&arrivals->connections, connection->connection % QUEUE_LIMIT, 0),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __builtin_trap();
                        }
                    );
                }
                QueueListener__notify(listener);
            }
        },
        none_do_nothing()
    );
    IpcPlatform__signal_event();
    // Detachment is committed: callers must never retry through their unmapped queue.
    // Failed backing frees stay quarantined and are retried by the next connect.
    match_result(
        QueueConnection__release(connection),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(MEMORY, "Queue backing quarantined after close: %i\n", code);
        }
    );
    return Success(QueueCode, QUEUE_OK);
}

Result(QueueCode) Queues__close(QueuesPtr self, ProcessPtr process, QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&self->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid)
            {
                return Queues__close_connection(self, connection, process);
            }
        },
        none_do_nothing()
    );
    return Error(QueueCode, queue_error, QUEUE_INVALID);
}

Result(ProcessPtr) Queues__client_process(QueuesPtr self, ProcessPtr server, QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&self->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid &&
                Queues__belongs_to(&connection->server, server))
            {
                QueueIdentityPtr identity = k_v_ptr(connection->memory, QueueIdentity);
                if (__atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) == 0 ||
                    __atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0)
                {
                    return Error(ProcessPtr, queue_error, QUEUE_DENIED);
                }
                match_optional(
                    connection->client,
                    some(client)
                    {
                        return Success(ProcessPtr, client);
                    },
                    none_do_nothing()
                );
            }
        },
        none_do_nothing()
    );
    return Error(ProcessPtr, queue_error, QUEUE_DENIED);
}

void Queues__exit(QueuesPtr self, ProcessPtr process)
{
    for_each_fa(
        QueueConnections,
        self->connections,
        index,
        value,
        {
            match_optional(
                QueueConnections__ptr_at(&self->connections, index),
                some(connection)
                {
                    if (connection->memory.valid && (Queues__belongs_to(&connection->client, process) ||
                                                     Queues__belongs_to(&connection->server, process)))
                    {
                        Queues__close_connection(self, connection, process);
                    }
                },
                none_do_nothing()
            );
        }
    );
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        value,
        {
            match_optional(
                QueueListeners__ptr_at(&self->listeners, index),
                some(listener)
                {
                    if (!Queues__belongs_to(&listener->process, process))
                    {
                        continue;
                    }
                    KernelServiceDirectory__remove_endpoint(&process->kernel->service_directory,
                                                            listener->endpoint);
                    PageTable__unmap(&process->page_table, process->kernel, listener->arrivals.virtual_addr,
                                     QUEUE_ARRIVALS_SIZE);
                    listener->process = None(ProcessPtr);
                    QueueListener__release(listener);
                },
                none_do_nothing()
            );
        }
    );
}

Result(Queue) Queues__connect_kernel(QueuesPtr self, QueueEndpointID endpoint, OptionalPtr(ProcessPtr) origin)
{
    match_optional(
        QueueListeners__ptr_at(&self->listeners, endpoint % QUEUE_LIMIT),
        some(listener)
        {
            if (!listener->arrivals.valid || listener->endpoint != endpoint)
            {
                return Error(Queue, queue_error, QUEUE_INVALID);
            }
            match_optional(
                listener->process,
                some(server)
                {
                    match_result(
                        Queues__connect_to(self, listener, server, &None(ProcessPtr), endpoint % QUEUE_LIMIT),
                        success(id)
                        {
                            match_optional(
                                QueueConnections__ptr_at(&self->connections, id % QUEUE_LIMIT),
                                some(connection)
                                {
                                    connection->kernel_origin = *origin;
                                    match_optional(
                                        *origin,
                                        some(process)
                                        {
                                            k_v_ptr(connection->memory, QueueIdentity)->origin_pid = process->pid;
                                        },
                                        none_do_nothing()
                                    );
                                    Address base = connection->memory.kernel_addr.virtual_addr;
                                    return Queue__attach((QueueIdentityPtr)base, (QueuePagePtr)(base + QUEUE_PAGE_SIZE),
                                                         (QueuePagePtr)(base + 2 * QUEUE_PAGE_SIZE), id,
                                                         QUEUE_KERNEL_PID);
                                },
                                none_do_nothing()
                            );
                        },
                        error(code, message)
                        {
                            return Error(Queue, message, code);
                        }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(Queue, queue_error, QUEUE_INVALID);
}

Result(QueueCode) Queues__close_kernel(QueuesPtr self, QueueConnectionID id)
{
    match_optional(
        QueueConnections__ptr_at(&self->connections, id % QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid && connection->kernel_client)
            {
                QueueIdentityPtr identity = k_v_ptr(connection->memory, QueueIdentity);
                __atomic_store_n(&identity->closed, 1, __ATOMIC_RELEASE);
                connection->kernel_client = false;
                IpcPlatform__signal_event();
                QueueConnection__release(connection);
                return Success(QueueCode, QUEUE_OK);
            }
        },
        none_do_nothing()
    );
    return Error(QueueCode, queue_error, QUEUE_INVALID);
}

static bool QueueConnection__kernel_client_of(QueueConnectionPtr self, void * context)
{
    ProcessPtr server = context;
    return self->kernel_client && self->memory.valid && Queues__belongs_to(&self->server, server) &&
           __atomic_load_n(&k_v_ptr(self->memory, QueueIdentity)->closed, __ATOMIC_ACQUIRE) == 0;
}

bool Queues__has_kernel_client(QueuesPtr self, ProcessPtr server)
{
    match_find_result(
        QueueConnections__find(&self->connections, QueueConnection__kernel_client_of, server),
        some(found)
        {
            return true;
        },
        none()
        {
            return false;
        }
    );
}

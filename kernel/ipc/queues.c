#include "queues.h"
#include "../arch/page_table.h"
#include "../kernel_core.h"

FixedArrayBodyDefine(QueueListeners);
FixedArrayBodyDefine(QueueConnections);

static BString queue_error = bstr("Atomic message queue operation failed");

static Result(QueueCode) Queues__close_connection(QueuesPtr self, QueueConnectionPtr connection, ProcessPtr process);

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
                                            QueueSlotIndex listener_index, UserAddress client_doorbell,
                                            UserAddress server_doorbell)
{
    return (QueueConnection){
        .memory = memory,
        .client_doorbell = client_doorbell,
        .server_doorbell = server_doorbell,
        .client = *client,
        .server = *server,
        .kernel_origin = None(ProcessPtr),
        .connection = connection,
        .delegated_from = 0,
        .listener_index = listener_index,
        .client_peer_doorbell_mapped = false,
        .server_peer_doorbell_mapped = false,
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

static Queue QueueConnection__queue_for_endpoint(QueueConnectionPtr self, bool client)
{
    Address base = self->memory.kernel_addr.virtual_addr;
    UserAddress peer_doorbell = client ? self->server_doorbell : self->client_doorbell;
    return Queue__new((QueueIdentityPtr)base, (QueuePagePtr)(base + (client ? 1 : 2) * QUEUE_PAGE_SIZE),
                      (QueuePagePtr)(base + (client ? 2 : 1) * QUEUE_PAGE_SIZE), self->connection, 0,
                      k_v_ptr(peer_doorbell, IpcDoorbell), &Queue__ignore_data_available);
}

static bool QueueConnection__has_unread_messages(QueueConnectionPtr self, void * context)
{
    ProcessPtr process = context;
    bool client = Queues__belongs_to(&self->client, process);
    if (!self->memory.valid || (!client && !Queues__belongs_to(&self->server, process)))
    {
        return false;
    }
    Queue queue = QueueConnection__queue_for_endpoint(self, client);
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

static bool QueueConnection__has_waitable_state(QueueConnectionPtr self, void * context)
{
    ProcessPtr process = context;
    bool client = Queues__belongs_to(&self->client, process);
    bool server = Queues__belongs_to(&self->server, process);
    if (!self->memory.valid || (!client && !server))
    {
        return false;
    }

    QueueIdentityPtr identity = k_v_ptr(self->memory, QueueIdentity);
    if (__atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0)
    {
        return true;
    }
    if (__atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) == 0)
    {
        return server;
    }
    return QueueConnection__has_unread_messages(self, context);
}

bool Queues__has_waitable_state(QueuesPtr self, ProcessPtr process)
{
    match_find_result(
        QueueConnections__find(&self->connections, QueueConnection__has_waitable_state, process),
        some(connection)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

Result(ProcessPtr) Queues__peer_process(QueuesPtr self, ProcessPtr process, QueueConnectionID connection)
{
    match_optional(
        QueueConnections__ptr_at(&self->connections, connection % QUEUE_LIMIT),
        some(stored)
        {
            if (!stored->memory.valid || stored->connection != connection)
            {
                return Error(ProcessPtr, queue_error, QUEUE_INVALID);
            }
            if (Queues__belongs_to(&stored->client, process))
            {
                match_optional(stored->server,
                    some(peer)
                    {
                        return Success(ProcessPtr, peer);
                    },
                    none_do_nothing()
                );
                return Error(ProcessPtr, queue_error, QUEUE_CLOSED);
            }
            if (Queues__belongs_to(&stored->server, process))
            {
                match_optional(stored->client,
                    some(peer)
                    {
                        return Success(ProcessPtr, peer);
                    },
                    none_do_nothing()
                );
                return Error(ProcessPtr, queue_error, QUEUE_CLOSED);
            }
        },
        none_do_nothing()
    );
    return Error(ProcessPtr, queue_error, QUEUE_DENIED);
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
    match_optional(self->process,
        some(process)
        {
            Process__ring_doorbell(process);
        },
        none_do_nothing()
    );
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

static Result(UserAddress) Queues__kernel_doorbell(QueuesPtr self, KernelPtr kernel)
{
    match_optional(self->kernel_doorbell,
        some(mapping)
        {
            return Success(UserAddress, mapping);
        },
        none_do_nothing()
    );
    match_result(
        Kernel__try_alloc_user(IPC_DOORBELL_ADDRESS, IPC_DOORBELL_PAGE_SIZE, true, true, false),
        success(mapping)
        {
            Kernel__memset(mapping.kernel_addr.virtual_addr, 0, IPC_DOORBELL_PAGE_SIZE);
            self->kernel_doorbell = Some(UserAddress, mapping);
            return Success(UserAddress, mapping);
        },
        error(code, message)
        {
            return Error(UserAddress, message, QUEUE_NO_MEMORY);
        }
    );
    return Error(UserAddress, queue_error, QUEUE_NO_MEMORY);
}

static void QueueConnection__map(QueueConnectionPtr self, ProcessPtr process, bool client)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        UserAddress page = UserAddress__relative(&self->memory, index * QUEUE_PAGE_SIZE);
        page.write = index == (client ? 1U : 2U);
        PageTable__map_shared(&process->page_table, process->kernel, page, QUEUE_PAGE_SIZE);
    }
    UserAddress peer = client ? self->server_doorbell : self->client_doorbell;
    UserAddress alias = UserAddress__new(QueueLayout__peer_doorbell_address(self->connection), peer.kernel_addr, true,
                                         true, false, true);
    PageTable__map_shared(&process->page_table, process->kernel, alias, QUEUE_PAGE_SIZE);
    if (client)
    {
        self->client_peer_doorbell_mapped = true;
    }
    else
    {
        self->server_peer_doorbell_mapped = true;
    }
}

static void QueueConnection__unmap_peer_doorbell(QueueConnectionPtr self, ProcessPtr process, bool client)
{
    bool mapped = client ? self->client_peer_doorbell_mapped : self->server_peer_doorbell_mapped;
    if (!mapped)
    {
        return;
    }
    PageTable__unmap(&process->page_table, process->kernel, QueueLayout__peer_doorbell_address(self->connection),
                     QUEUE_PAGE_SIZE);
    if (client)
    {
        self->client_peer_doorbell_mapped = false;
    }

    else
    {
        self->server_peer_doorbell_mapped = false;
    }
}

static void QueueConnection__unmap(QueueConnectionPtr self, ProcessPtr process, bool client)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        PageTable__unmap(&process->page_table, process->kernel, self->memory.virtual_addr + index * QUEUE_PAGE_SIZE,
                         QUEUE_PAGE_SIZE);
    }
    QueueConnection__unmap_peer_doorbell(self, process, client);
}

static Result(Address) QueueConnection__release(QueueConnectionPtr self)
{
    if (self->memory.valid && !self->kernel_client && is_nothing(self->client) && is_nothing(self->server))
    {
        match_result(
            Kernel__free(self->memory.kernel_addr.virtual_addr),
            success(_)
            {
                *self = QueueConnection__new(NULL_USER_ADDRESS, &None(ProcessPtr), &None(ProcessPtr), 0, 0,
                                             NULL_USER_ADDRESS, NULL_USER_ADDRESS);
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
    match_result(
        Process__doorbell(process),
        success_do_nothing(),
        error(code, message)
        {
            return Error(Address, message, QUEUE_NO_MEMORY);
        }
    );
    for_each_fa(
        QueueListeners,
        self->listeners,
        index,
        listener,
        {
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

    match_result(
        Queues__next_id(self, selected),
        success(endpoint)
        {
            match_result(
                Kernel__try_alloc_user(address, QUEUE_ARRIVALS_SIZE, true, false, false),
                success(memory)
                {
                    Kernel__memset(memory.kernel_addr.virtual_addr, 0, QUEUE_ARRIVALS_SIZE);
                    QueueArrivalsPtr arrivals = k_v_ptr(memory, QueueArrivals);
                    arrivals->endpoint = endpoint;
                    QueueListeners__set(&self->listeners, selected,
                                        QueueListener__new(memory, &None(ProcessPtr), endpoint));
                    match_result(
                        KernelServiceDirectory__publish_endpoint(&process->kernel->service_directory, process, name,
                                                                 endpoint),
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
        },
        error(code, message)
        {
            return Error(Address, message, code);
        }
    );
    return Error(Address, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static Result(UserAddress) Queues__client_doorbell(QueuesPtr self, ProcessPtr server, OptionalPtr(ProcessPtr) client)
{
    match_optional(
        *client,
        some(process)
        {
            return Process__doorbell(process);
        },
        none()
        {
            return Queues__kernel_doorbell(self, server->kernel);
        }
    );
    return Error(UserAddress, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
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
                               : QUEUE_CLIENT_CONNECTION_LIMIT;
        },
        none_do_nothing()
    );
    match_result(
        Process__doorbell(server),
        success(server_doorbell)
        {
            match_result(
                Queues__client_doorbell(self, server, client),
                success(client_doorbell)
                {
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
                    match_result(
                        Queues__next_id(self, selected),
                        success(id)
                        {
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
                                        QueueConnection__new(memory, client, &Some(ProcessPtr, server), id,
                                                             listener_index, client_doorbell, server_doorbell);
                                    connection.kernel_client = is_nothing((*client));
                                    match_optional(
                                        *client,
                                        some(process)
                                        {
                                            QueueConnection__map(&connection, process, true);
                                        },
                                        none_do_nothing()
                                    );
                                    QueueConnection__map(&connection, server, false);
                                    QueueConnections__set(&self->connections, selected, connection);
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
                        },
                        error(code, message)
                        {
                            return Error(QueueConnectionID, message, code);
                        }
                    );
                    return Error(QueueConnectionID, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
                },
                error(code, message)
                {
                    return Error(QueueConnectionID, message, QUEUE_NO_MEMORY);
                }
            );
            return Error(QueueConnectionID, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
        },
        error(code, message)
        {
            return Error(QueueConnectionID, message, QUEUE_NO_MEMORY);
        }
    );
    return Error(QueueConnectionID, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
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

Result(QueueConnectionID)
    Queues__delegate(QueuesPtr self, ProcessPtr manager, QueueConnectionID client_connection, QueueEndpointID endpoint)
{
    if (!Process__has_capability(manager, PROCESS_CAP_MANAGE_SHARED_PAGES))
    {
        return Error(QueueConnectionID, queue_error, QUEUE_DENIED);
    }

    match_result(
        Queues__client_process(self, manager, client_connection),
        success(client)
        {
            match_result(
                Queues__connect(self, client, endpoint),
                success(connection)
                {
                    match_optional(
                        QueueConnections__ptr_at(&self->connections, connection % QUEUE_LIMIT),
                        some(stored)
                        {
                            k_v_ptr(stored->memory, QueueIdentity)->channel = 1;
                            stored->delegated_from = client_connection;
                            return Success(QueueConnectionID, connection);
                        },
                        none_do_nothing()
                    );
                    return Error(QueueConnectionID, queue_error, QUEUE_INVALID);
                },
                error(code, message)
                {
                    return Error(QueueConnectionID, message, code);
                }
            );
        },
        error(code, message)
        {
            return Error(QueueConnectionID, message, code);
        }
    );
    return Error(QueueConnectionID, queue_error, QUEUE_DENIED);
}

Result(QueueCode) Queues__cancel_delegation(QueuesPtr self, ProcessPtr manager, QueueConnectionID client_connection,
                                            QueueConnectionID delegated)
{
    if (!Process__has_capability(manager, PROCESS_CAP_MANAGE_SHARED_PAGES))
    {
        return Error(QueueCode, queue_error, QUEUE_DENIED);
    }

    QueueConnectionPtr delegated_connection;
    match_optional(
        QueueConnections__ptr_at(&self->connections, delegated % QUEUE_LIMIT),
        some(connection)
        {
            delegated_connection = connection;
        },
        none()
        {
            return Error(QueueCode, queue_error, QUEUE_INVALID);
        }
    );
    bool authorized =
        delegated_connection->connection == delegated && delegated_connection->memory.valid &&
        delegated_connection->delegated_from == client_connection &&
        __atomic_load_n(&k_v_ptr(delegated_connection->memory, QueueIdentity)->channel, __ATOMIC_ACQUIRE) != 0;
    match_optional(
        QueueConnections__ptr_at(&self->connections, client_connection % QUEUE_LIMIT),
        some(control)
        {
            authorized = authorized && control->connection == client_connection && control->memory.valid &&
                         Queues__belongs_to(&control->server, manager);
        },
        none()
        {
            authorized = false;
        }
    );
    if (!authorized)
    {
        return Error(QueueCode, queue_error, QUEUE_DENIED);
    }

    match_optional(
        delegated_connection->client,
        some(client)
        {
            match_result(
                Queues__close_connection(self, delegated_connection, client),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(QueueCode, message, code);
                }
            );
        },
        none_do_nothing()
    );
    match_optional(
        QueueConnections__ptr_at(&self->connections, delegated % QUEUE_LIMIT),
        some(connection)
        {
            match_optional(
                connection->server,
                some(server)
                {
                    return Queues__close_connection(self, connection, server);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Success(QueueCode, QUEUE_OK);
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
                IpcEvent__ring(IpcDoorbell__mutable_event(k_v_ptr(connection->client_doorbell, IpcDoorbell)));
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
    QueueConnection__unmap(connection, process, client);
    if (client)
    {
        match_optional(
            connection->server,
            some(peer)
            {
                Process__ring_doorbell(peer);
                QueueConnection__unmap_peer_doorbell(connection, peer, false);
            },
            none_do_nothing()
        );
        connection->client = None(ProcessPtr);
    }
    if (server)
    {
        match_optional(
            connection->client,
            some(peer)
            {
                Process__ring_doorbell(peer);
                QueueConnection__unmap_peer_doorbell(connection, peer, true);
            },
            none_do_nothing()
        );
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
                                    k_v_ptr(connection->memory, QueueIdentity)->channel = 1;
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
                                                         QUEUE_KERNEL_PID,
                                                         k_v_ptr(connection->server_doorbell, IpcDoorbell),
                                                         &Queue__ignore_data_available);
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
                match_optional(
                    connection->server,
                    some(server)
                    {
                        Process__ring_doorbell(server);
                        QueueConnection__unmap_peer_doorbell(connection, server, false);
                    },
                    none_do_nothing()
                );
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

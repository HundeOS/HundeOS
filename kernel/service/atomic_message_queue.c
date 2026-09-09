#include "atomic_message_queue.h"
#include "../arch/page_table.h"
#include "../kernel_core.h"

FixedArrayBodyDefine(AtomicMessageQueueListeners);
FixedArrayBodyDefine(AtomicMessageQueueConnections);

static BString queue_error = bstr("Atomic message queue operation failed");

bool AtomicMessageQueueListener__equals(void * first, void * second)
{
    AtomicMessageQueueListenerPtr a = first;
    AtomicMessageQueueListenerPtr b = second;
    return a->endpoint == b->endpoint;
}

bool AtomicMessageQueueConnection__equals(void * first, void * second)
{
    AtomicMessageQueueConnectionPtr a = first;
    AtomicMessageQueueConnectionPtr b = second;
    return a->connection == b->connection;
}

static AtomicMessageQueueListener AtomicMessageQueueListener__new(UserAddress arrivals, OptionalPtr(ProcessPtr) process,
                                                                  AtomicMessageQueueEndpointID endpoint)
{
    return (AtomicMessageQueueListener){.arrivals = arrivals, .process = *process, .endpoint = endpoint};
}

static AtomicMessageQueueConnection
AtomicMessageQueueConnection__new(UserAddress memory, OptionalPtr(ProcessPtr) client, OptionalPtr(ProcessPtr) server,
                                  AtomicMessageQueueConnectionID connection, AtomicMessageQueueSlotIndex listener_index)
{
    return (AtomicMessageQueueConnection){
        .memory = memory,
        .client = *client,
        .server = *server,
        .connection = connection,
        .listener_index = listener_index,
    };
}

static bool AtomicMessageQueues__belongs_to(OptionalPtr(ProcessPtr) owner, ProcessPtr process)
{
    match_optional(*owner,
        some(value) { return value == process; },
        none_do_nothing()
    );
    return false;
}

static bool AtomicMessageQueues__range_available(ProcessPtr process, Address address, size_t size)
{
    for_each_ll(UserAllocations, process->user_allocations, allocation, {
        Address start = allocation.address.virtual_addr;
        if (start <= address ? address - start < allocation.size : start - address < size)
        {
            return false;
        }
    });
    return true;
}

static void AtomicMessageQueueListener__notify(AtomicMessageQueueListenerPtr self)
{
    AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(self->arrivals, AtomicMessageQueueArrivals);
    __atomic_add_fetch(&arrivals->epoch, 1, __ATOMIC_RELEASE);
    ServicePlatform__signal_event();
}

static Result(AtomicMessageQueueHandleID)
    AtomicMessageQueues__next_id(AtomicMessageQueuesPtr self, AtomicMessageQueueSlotIndex index)
{
    if (self->generation >= (INT64_MAX / ATOMIC_MESSAGE_QUEUE_LIMIT) - 1)
    {
        return Error(AtomicMessageQueueHandleID, queue_error, ATOMIC_MESSAGE_QUEUE_FULL);
    }
    self->generation++;
    return Success(AtomicMessageQueueHandleID, self->generation * ATOMIC_MESSAGE_QUEUE_LIMIT + index);
}

static void AtomicMessageQueueConnection__map(AtomicMessageQueueConnectionPtr self, ProcessPtr process, bool client)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        UserAddress page = UserAddress__relative(&self->memory, index * HUND_SERVICE_PAGE_SIZE);
        page.write = index == (client ? 1U : 2U);
        PageTable__map(&process->page_table, process->kernel, page, HUND_SERVICE_PAGE_SIZE);
    }
}

static void AtomicMessageQueueConnection__unmap(AtomicMessageQueueConnectionPtr self, ProcessPtr process)
{
    for (uint64_t index = 0; index < 3; index++)
    {
        PageTable__unmap(&process->page_table, process->kernel,
                         self->memory.virtual_addr + index * HUND_SERVICE_PAGE_SIZE, HUND_SERVICE_PAGE_SIZE);
    }
}

static Result(Address) AtomicMessageQueueConnection__release(AtomicMessageQueueConnectionPtr self)
{
    if (self->memory.valid && is_nothing(self->client) && is_nothing(self->server))
    {
        match_result(
            Kernel__free(self->memory.kernel_addr.virtual_addr),
            success(_)
            {
                *self =
                    AtomicMessageQueueConnection__new(NULL_USER_ADDRESS, &None(ProcessPtr), &None(ProcessPtr), 0, 0);
            },
            error(code, message) { return Error(Address, message, ATOMIC_MESSAGE_QUEUE_NO_MEMORY); }
        );
    }
    return Success(Address, ATOMIC_MESSAGE_QUEUE_OK);
}

static void AtomicMessageQueueListener__release(AtomicMessageQueueListenerPtr self)
{
    if (!self->arrivals.valid || !is_nothing(self->process))
    {
        return;
    }
    match_result(
        Kernel__free(self->arrivals.kernel_addr.virtual_addr),
        success(_) { *self = AtomicMessageQueueListener__new(NULL_USER_ADDRESS, &None(ProcessPtr), 0); },
        error(code, message) { __d_printf(IODEV, "Queue listener backing quarantined: %i\n", code); }
    );
}

Result(Address) AtomicMessageQueues__listen(AtomicMessageQueuesPtr self, ProcessPtr process, ConstBStringPtr name)
{
    if (!ProcessClass__is_service(process->process_class) ||
        !Process__has_capability(process, PROCESS_CAP_BECOME_SERVICE))
    {
        return Error(Address, queue_error, ATOMIC_MESSAGE_QUEUE_DENIED);
    }
    for_each_fa(AtomicMessageQueueListeners, self->listeners, index, listener, {
        if (AtomicMessageQueues__belongs_to(&listener.process, process))
        {
            return Error(Address, queue_error, ATOMIC_MESSAGE_QUEUE_OCCUPIED);
        }
        match_optional(
            AtomicMessageQueueListeners__ptr_at(&self->listeners, index),
            some(slot) { AtomicMessageQueueListener__release(slot); },
            none_do_nothing()
        );
    });
    uint64_t selected = AtomicMessageQueueListeners__len(&self->listeners);
    for_each_fa(AtomicMessageQueueListeners, self->listeners, index, listener, {
        if (!listener.arrivals.valid)
        {
            selected = index;
            break;
        }
    });
    Address address = ATOMIC_MESSAGE_QUEUE_LISTENER_BASE + selected * HUND_SERVICE_PAGE_SIZE;
    if (selected >= ATOMIC_MESSAGE_QUEUE_LIMIT ||
        !AtomicMessageQueues__range_available(process, address, HUND_SERVICE_PAGE_SIZE))
    {
        return Error(Address, queue_error, ATOMIC_MESSAGE_QUEUE_FULL);
    }
    AtomicMessageQueueEndpointID endpoint;
    match_result(
        AtomicMessageQueues__next_id(self, selected),
        success(value) { endpoint = value; },
        error(code, message) { return Error(Address, message, code); }
    );
    match_result(
        Kernel__try_alloc_user(address, HUND_SERVICE_PAGE_SIZE, true, false, false),
        success(memory)
        {
            Kernel__memset(memory.kernel_addr.virtual_addr, 0, HUND_SERVICE_PAGE_SIZE);
            AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(memory, AtomicMessageQueueArrivals);
            arrivals->endpoint = endpoint;
            AtomicMessageQueueListeners__set(&self->listeners, selected,
                                             AtomicMessageQueueListener__new(memory, &None(ProcessPtr), endpoint));
            match_result(
                KernelServiceDirectory__publish_endpoint(&process->kernel->service_directory, process, name, endpoint),
                success_do_nothing(),
                error(code, message)
                {
                    match_optional(
                        AtomicMessageQueueListeners__ptr_at(&self->listeners, selected),
                        some(slot) { AtomicMessageQueueListener__release(slot); },
                        none_do_nothing()
                    );
                    return Error(Address, message, code);
                }
            );
            AtomicMessageQueueListeners__set(
                &self->listeners, selected,
                AtomicMessageQueueListener__new(memory, &Some(ProcessPtr, process), endpoint));
            PageTable__map(&process->page_table, process->kernel, memory, HUND_SERVICE_PAGE_SIZE);
            return Success(Address, address);
        },
        error(code, message) { return Error(Address, message, ATOMIC_MESSAGE_QUEUE_NO_MEMORY); }
    );
    return Error(Address, queue_error, ATOMIC_MESSAGE_QUEUE_NO_MEMORY);
}

static Result(AtomicMessageQueueConnectionID)
    AtomicMessageQueues__connect_to(AtomicMessageQueuesPtr self, AtomicMessageQueueListenerPtr listener,
                                    ProcessPtr server, ProcessPtr client, AtomicMessageQueueSlotIndex listener_index)
{
    if (client == server)
    {
        return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    uint64_t selected = AtomicMessageQueueConnections__len(&self->connections);
    uint64_t client_count = 0;
    for_each_fa(AtomicMessageQueueConnections, self->connections, index, connection, {
        if (AtomicMessageQueues__belongs_to(&connection.client, client))
        {
            client_count++;
        }
        if (!connection.memory.valid)
        {
            selected = index;
        }
    });
    if (selected >= ATOMIC_MESSAGE_QUEUE_LIMIT || client_count >= 4)
    {
        return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_FULL);
    }
    Address address = AtomicMessageQueueLayout__address(selected);
    if (!AtomicMessageQueues__range_available(client, address, ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE) ||
        !AtomicMessageQueues__range_available(server, address, ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE))
    {
        return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_INVALID);
    }
    AtomicMessageQueueConnectionID id;
    match_result(
        AtomicMessageQueues__next_id(self, selected),
        success(value) { id = value; },
        error(code, message) { return Error(AtomicMessageQueueConnectionID, message, code); }
    );
    match_result(
        Kernel__try_alloc_user(address, ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE, true, false, false),
        success(memory)
        {
            Kernel__memset(memory.kernel_addr.virtual_addr, 0, ATOMIC_MESSAGE_QUEUE_CONNECTION_SIZE);
            AtomicMessageQueueIdentityPtr identity = k_v_ptr(memory, AtomicMessageQueueIdentity);
            identity->connection = id;
            identity->endpoint = listener->endpoint;
            identity->client_pid = client->pid;
            identity->server_pid = server->pid;
            AtomicMessageQueueConnection connection = AtomicMessageQueueConnection__new(
                memory, &Some(ProcessPtr, client), &Some(ProcessPtr, server), id, listener_index);
            AtomicMessageQueueConnections__set(&self->connections, selected, connection);
            AtomicMessageQueueConnection__map(&connection, client, true);
            AtomicMessageQueueConnection__map(&connection, server, false);
            AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, AtomicMessageQueueArrivals);
            match_result(
                AtomicMessageQueueConnectionSlots__set(&arrivals->connections, selected, id),
                success_do_nothing(),
                error(code, message) { __builtin_trap(); }
            );
            AtomicMessageQueueListener__notify(listener);
            return Success(AtomicMessageQueueConnectionID, id);
        },
        error(code, message)
        {
            return Error(AtomicMessageQueueConnectionID, message, ATOMIC_MESSAGE_QUEUE_NO_MEMORY);
        }
    );
    return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_NO_MEMORY);
}

Result(AtomicMessageQueueConnectionID)
    AtomicMessageQueues__connect(AtomicMessageQueuesPtr self, ProcessPtr process, AtomicMessageQueueEndpointID endpoint)
{
    // Failed frees remain quarantined and are retried before allocating new slots.
    for_each_fa(AtomicMessageQueueConnections, self->connections, index, value, {
        match_optional(
            AtomicMessageQueueConnections__ptr_at(&self->connections, index),
            some(connection) { AtomicMessageQueueConnection__release(connection); },
            none_do_nothing()
        );
    });
    match_optional(
        AtomicMessageQueueListeners__ptr_at(&self->listeners, endpoint % ATOMIC_MESSAGE_QUEUE_LIMIT),
        some(listener)
        {
            if (!listener->arrivals.valid || listener->endpoint != endpoint)
            {
                return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_INVALID);
            }
            match_optional(
                listener->process,
                some(server)
                {
                    return AtomicMessageQueues__connect_to(self, listener, server, process,
                                                           endpoint % ATOMIC_MESSAGE_QUEUE_LIMIT);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
    return Error(AtomicMessageQueueConnectionID, queue_error, ATOMIC_MESSAGE_QUEUE_INVALID);
}

Result(AtomicMessageQueueCode)
    AtomicMessageQueues__accept(AtomicMessageQueuesPtr self, ProcessPtr process, AtomicMessageQueueConnectionID id)
{
    match_optional(
        AtomicMessageQueueConnections__ptr_at(&self->connections, id % ATOMIC_MESSAGE_QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid &&
                AtomicMessageQueues__belongs_to(&connection->server, process))
            {
                AtomicMessageQueueIdentityPtr identity = k_v_ptr(connection->memory, AtomicMessageQueueIdentity);
                if (__atomic_load_n(&identity->closed, __ATOMIC_ACQUIRE) != 0)
                {
                    return Error(AtomicMessageQueueCode, queue_error, ATOMIC_MESSAGE_QUEUE_CLOSED);
                }
                if (__atomic_load_n(&identity->accepted, __ATOMIC_ACQUIRE) != 0)
                {
                    return Error(AtomicMessageQueueCode, queue_error, ATOMIC_MESSAGE_QUEUE_OCCUPIED);
                }
                __atomic_store_n(&identity->accepted, 1, __ATOMIC_RELEASE);
                ServicePlatform__signal_event();
                return Success(AtomicMessageQueueCode, ATOMIC_MESSAGE_QUEUE_OK);
            }
        },
        none_do_nothing()
    );
    return Error(AtomicMessageQueueCode, queue_error, ATOMIC_MESSAGE_QUEUE_DENIED);
}

static Result(AtomicMessageQueueCode)
    AtomicMessageQueues__close_connection(AtomicMessageQueuesPtr self, AtomicMessageQueueConnectionPtr connection,
                                          ProcessPtr process)
{
    bool client = AtomicMessageQueues__belongs_to(&connection->client, process);
    bool server = AtomicMessageQueues__belongs_to(&connection->server, process);
    if (!client && !server)
    {
        return Error(AtomicMessageQueueCode, queue_error, ATOMIC_MESSAGE_QUEUE_DENIED);
    }
    AtomicMessageQueueIdentityPtr identity = k_v_ptr(connection->memory, AtomicMessageQueueIdentity);
    __atomic_store_n(&identity->closed, 1, __ATOMIC_RELEASE);
    AtomicMessageQueueConnection__unmap(connection, process);
    if (client)
    {
        connection->client = None(ProcessPtr);
    }
    if (server)
    {
        connection->server = None(ProcessPtr);
    }
    match_optional(
        AtomicMessageQueueListeners__ptr_at(&self->listeners, connection->listener_index),
        some(listener)
        {
            if (listener->arrivals.valid && listener->endpoint == identity->endpoint)
            {
                AtomicMessageQueueArrivalsPtr arrivals = k_v_ptr(listener->arrivals, AtomicMessageQueueArrivals);
                if (server)
                {
                    match_result(
                        AtomicMessageQueueConnectionSlots__set(&arrivals->connections,
                                                               connection->connection % ATOMIC_MESSAGE_QUEUE_LIMIT, 0),
                        success_do_nothing(),
                        error(code, message) { __builtin_trap(); }
                    );
                }
                AtomicMessageQueueListener__notify(listener);
            }
        },
        none_do_nothing()
    );
    ServicePlatform__signal_event();
    // Detachment is committed: callers must never retry through their unmapped queue.
    // Failed backing frees stay quarantined and are retried by the next connect.
    match_result(
        AtomicMessageQueueConnection__release(connection),
        success_do_nothing(),
        error(code, message) { __d_printf(IODEV, "Queue backing quarantined after close: %i\n", code); }
    );
    return Success(AtomicMessageQueueCode, ATOMIC_MESSAGE_QUEUE_OK);
}

Result(AtomicMessageQueueCode)
    AtomicMessageQueues__close(AtomicMessageQueuesPtr self, ProcessPtr process, AtomicMessageQueueConnectionID id)
{
    match_optional(
        AtomicMessageQueueConnections__ptr_at(&self->connections, id % ATOMIC_MESSAGE_QUEUE_LIMIT),
        some(connection)
        {
            if (connection->connection == id && connection->memory.valid)
            {
                return AtomicMessageQueues__close_connection(self, connection, process);
            }
        },
        none_do_nothing()
    );
    return Error(AtomicMessageQueueCode, queue_error, ATOMIC_MESSAGE_QUEUE_INVALID);
}

void AtomicMessageQueues__exit(AtomicMessageQueuesPtr self, ProcessPtr process)
{
    for_each_fa(AtomicMessageQueueConnections, self->connections, index, value, {
        match_optional(
            AtomicMessageQueueConnections__ptr_at(&self->connections, index),
            some(connection)
            {
                if (connection->memory.valid && (AtomicMessageQueues__belongs_to(&connection->client, process) ||
                                                 AtomicMessageQueues__belongs_to(&connection->server, process)))
                {
                    AtomicMessageQueues__close_connection(self, connection, process);
                }
            },
            none_do_nothing()
        );
    });
    for_each_fa(AtomicMessageQueueListeners, self->listeners, index, value, {
        match_optional(
            AtomicMessageQueueListeners__ptr_at(&self->listeners, index),
            some(listener)
            {
                if (!AtomicMessageQueues__belongs_to(&listener->process, process))
                {
                    continue;
                }
                KernelServiceDirectory__remove_endpoint(&process->kernel->service_directory, listener->endpoint);
                PageTable__unmap(&process->page_table, process->kernel, listener->arrivals.virtual_addr,
                                 HUND_SERVICE_PAGE_SIZE);
                listener->process = None(ProcessPtr);
                AtomicMessageQueueListener__release(listener);
            },
            none_do_nothing()
        );
    });
}

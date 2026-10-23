#include "trace_buffer.h"
#include "io_latency.h"

#include "buffer/client.h"
#include "channel/server.h"
#include "ipc/ipc_platform.h"
#include "ipc/io_trace.h"
#include "ipc/trace_protocol.h"
#include "process/process_registry.h"
#include "services/service_directory.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/syslog.h"
#include "trace/io_trace.h"
#include "types/fixed_array.h"

#define IO_TRACE_DRAIN_BATCH 64U
#define IO_TRACE_REAP_USEC 1000000ULL
#define IO_TRACE_SOURCE_GRACE_USEC 5000000ULL

typedef struct TraceClient
{
    ChannelServer channel;
    TraceReply pending_reply;
    bool pending;
} TraceClient;

typedef TraceClient * TraceClientPtr;
OptionalDef(TraceClient);

static TraceClient TraceClient__new(ChannelServerPtr channel)
{
    return (TraceClient){.channel = *channel};
}

static bool TraceClient__equals(void * first, void * second)
{
    TraceClientPtr left = first;
    TraceClientPtr right = second;
    return ChannelServer__connection(&left->channel) == ChannelServer__connection(&right->channel);
}

FixedArrayDefine(TraceClient, QUEUE_LIMIT, TraceClients, &TraceClient__equals);
FixedArrayBodyDefine(TraceClients);
typedef TraceClients * TraceClientsPtr;

typedef struct TraceSource
{
    uint64_t pid;
    BufferDescriptor buffer;
    uint64_t cursor;
    uint64_t missing_since_usec;
} TraceSource;

typedef TraceSource * TraceSourcePtr;
OptionalDef(TraceSource);

static TraceSource TraceSource__new(uint64_t pid, BufferDescriptor buffer)
{
    return (TraceSource){.pid = pid, .buffer = buffer};
}

static bool TraceSource__equals(void * first, void * second)
{
    TraceSourcePtr left = first;
    TraceSourcePtr right = second;
    return left->pid == right->pid;
}

static bool TraceSource__matches_pid(TraceSourcePtr self, void * context)
{
    return self->pid == *(uint64_t *)context;
}

FixedArrayDefine(TraceSource, BUFFER_LIMIT, TraceSources, &TraceSource__equals);
FixedArrayBodyDefine(TraceSources);

typedef struct TraceService
{
    ConstProcessRegistryPtr process_registry;
    QueueArrivalsPtr arrivals;
    TraceClients clients;
    TraceSources sources;
    TraceBuffer io_records;
    IOLatencyTracker io_latency;
    IOHandle io_log;
    uint64_t reap_at_usec;
    bool io_write_failure_reported;
} TraceService;

typedef TraceService * TraceServicePtr;

static bool TraceSource__drain(TraceServicePtr self, TraceSourcePtr source);
static bool TraceService__flush_io(TraceServicePtr self);

static TraceService TraceService__new(ConstProcessRegistryPtr process_registry, QueueArrivalsPtr arrivals)
{
    return (TraceService){
        .process_registry = process_registry,
        .arrivals = arrivals,
        .clients = TraceClients__new(),
        .sources = TraceSources__new(),
        .io_records = TraceBuffer__new(),
        .io_latency = IOLatencyTracker__new(),
        .io_log = IO_NOT_OPEN,
    };
}

static bool TraceClient__matches_connection(TraceClientPtr self, void * context)
{
    return ChannelServer__connection(&self->channel) == *(QueueConnectionIDPtr)context;
}

static bool TraceClients__contains_connection(TraceClientsPtr self, QueueConnectionID connection)
{
    match_find_result(
        TraceClients__find(self, TraceClient__matches_connection, &connection),
        some(client)
        {
            return true;
        },
        none_do_nothing()
    );
    return false;
}

static bool TraceClient__reply(TraceClientPtr self, TraceReply reply)
{
    match_result(
        ChannelServer__reply(&self->channel, (ConstChannelDataPtr)&reply, sizeof(reply)),
        success(code)
        {
            return true;
        },
        error(code, message)
        {
            if (code == CHANNEL_PENDING)
            {
                self->pending_reply = reply;
                self->pending = true;
                return true;
            }
            return false;
        }
    );
    return false;
}

static bool TraceClient__flush_reply(TraceClientPtr self)
{
    if (!self->pending)
    {
        return false;
    }
    match_result(
        ChannelServer__reply(&self->channel, (ConstChannelDataPtr)&self->pending_reply, sizeof(self->pending_reply)),
        success(code)
        {
            self->pending = false;
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static Optional(TraceSource) TraceService__find_io_source(TraceServicePtr self, uint64_t pid)
{
    match_find_result(
        TraceSources__find(&self->sources, &TraceSource__matches_pid, &pid),
        some(source)
        {
            return Some(TraceSource, source);
        },
        none_do_nothing()
    );
    return None(TraceSource);
}

static TraceStatus TraceService__drain_sources(TraceServicePtr self, uint64_t pid)
{
    bool registered = false;
    match_find_result_ptr(
        TraceSources__find(&self->sources, &TraceSource__matches_pid, &pid),
        some(source)
        {
            registered = true;
        },
        none_do_nothing()
    );
    if (!registered)
    {
        return TRACE_UNAVAILABLE;
    }

    while (!TraceBuffer__is_empty(&self->io_records))
    {
        if (!TraceService__flush_io(self))
        {
            return TRACE_IO_FAILED;
        }
    }

    for_each_fa_ptr(
        TraceSources,
        &self->sources,
        index,
        value,
        {
            match_optional(
                TraceSources__ptr_at(&self->sources, index),
                some(source)
                {
                    IOTraceRingPtr ring = (IOTraceRingPtr)source->buffer.address;
                    uint64_t produced = IOTraceRing__produced(ring);
                    while (source->cursor < produced)
                    {
                        if (!TraceSource__drain(self, source))
                        {
                            return TRACE_UNAVAILABLE;
                        }
                        while (!TraceBuffer__is_empty(&self->io_records))
                        {
                            if (!TraceService__flush_io(self))
                            {
                                return TRACE_IO_FAILED;
                            }
                        }
                    }
                },
                none_do_nothing()
            );
        }
    );
    return TRACE_OK;
}

static TraceReply TraceService__register_io(TraceServicePtr self, TraceClientPtr client)
{
    TraceReply reply = TraceReply__new(TRACE_OK);
    uint64_t pid = ChannelServer__transport_pid(&client->channel);
    match_optional(
        TraceService__find_io_source(self, pid),
        some(source)
        {
            reply.buffer = source.buffer;
            return reply;
        },
        none_do_nothing()
    );
    match_result(
        BufferClient__acquire(&bstr(HUND_VERFOLGEN_SERVICE), pid, BUFFER_WRITE),
        success(buffer)
        {
            TraceSource source = TraceSource__new(pid, buffer);
            match_result(
                TraceSources__push(&self->sources, source),
                success(code)
                {
                    reply.buffer = buffer;
                },
                error(code, message)
                {
                    BufferClient__release(&buffer);
                    reply.status = TRACE_UNAVAILABLE;
                }
            );
        },
        error(code, message)
        {
            reply.status = TRACE_UNAVAILABLE;
        }
    );
    return reply;
}

static TraceReply TraceService__handle_request(TraceServicePtr self, TraceClientPtr client,
                                               ConstTraceRequestPtr request)
{
    if (request->operation == TRACE_OPERATION_REGISTER_IO)
    {
        return TraceService__register_io(self, client);
    }

    if (request->operation == TRACE_OPERATION_DRAIN_IO)
    {
        return TraceReply__new(TraceService__drain_sources(self, ChannelServer__transport_pid(&client->channel)));
    }
    return TraceReply__new(TRACE_INVALID_REQUEST);
}

static bool TraceService__handle_client(TraceServicePtr self, TraceClientPtr client)
{
    bool work = TraceClient__flush_reply(client);
    if (client->pending)
    {
        return work;
    }

    match_result(
        ChannelServer__poll(&client->channel),
        success(optional_request)
        {
            match_optional(
                optional_request,
                some(metadata)
                {
                    TraceRequest request;
                    TraceReply reply = TraceReply__new(TRACE_INVALID_REQUEST);
                    if (metadata.size == sizeof(request) &&
                        ChannelServer__read_request(&client->channel, (ChannelDataPtr)&request, sizeof(request)))
                    {
                        reply = TraceService__handle_request(self, client, &request);
                    }
                    TraceClient__reply(client, reply);
                    return true;
                },
                none_do_nothing()
            );
        },
        error(code, message)
        {
            return work;
        }
    );
    return work;
}

static bool TraceService__accept_clients(TraceServicePtr self, QueueParticipantID warden_pid)
{
    bool work = false;
    for_each_atomic_fa_ptr(
        QueueConnectionSlots,
        &self->arrivals->connections,
        index,
        connection,
        {
            if (connection == 0 || TraceClients__contains_connection(&self->clients, connection))
            {
                continue;
            }
            match_result(
                ChannelServer__accept(connection),
                success(channel)
                {
                    TraceClient client = TraceClient__new(&channel);
                    match_result(
                        TraceClients__push(&self->clients, client),
                        success_do_nothing(),
                        error(code, message)
                        {
                            ChannelServer__close(&channel);
                        }
                    );
                },
                error(code, message)
                {
                    QueueConnection__close(connection);
                }
            );
            work = true;
        }
    );
    return work;
}

static bool TraceService__handle_clients(TraceServicePtr self)
{
    bool work = false;
    for_each_fa_ptr(
        TraceClients,
        &self->clients,
        index,
        value,
        {
            match_optional(
                TraceClients__ptr_at(&self->clients, index),
                some(client)
                {
                    if (ChannelServer__status(&client->channel) != QUEUE_OK)
                    {
                        TraceClient removed = *client;
                        TraceClients__remove(&self->clients, removed);
                        ChannelServer__close(&removed.channel);
                        work = true;
                        break;
                    }
                    work |= TraceService__handle_client(self, client);
                },
                none_do_nothing()
            );
        }
    );
    return work;
}

static bool TraceSource__drain(TraceServicePtr self, TraceSourcePtr source)
{
    IOTraceRingPtr ring = (IOTraceRingPtr)source->buffer.address;
    if (!IOTraceRing__ready(ring))
    {
        return false;
    }
    uint64_t produced = IOTraceRing__produced(ring);
    uint64_t oldest = IOTraceRing__oldest(ring, produced);
    if (source->cursor < oldest)
    {
        uint64_t dropped = oldest - source->cursor;
        BString notice = BString__sprintf(&bstr("IO-LATENCY-DROPPED pid=%llu events=%llu"), source->pid, dropped);
        TraceBuffer__append(&self->io_records, &notice);
        source->cursor = oldest;
    }
    bool work = false;
    size_t drained = 0;
    while (source->cursor < produced && drained < IO_TRACE_DRAIN_BATCH)
    {
        bool read = false;
        match_optional(
            IOTraceRing__read(ring, source->cursor),
            some(event)
            {
                read = true;
                if (event.ticket.metadata.origin_pid == process_current_pid())
                {
                    source->cursor++;
                    drained++;
                    work = true;
                    continue;
                }
                match_optional(
                    IOLatencyTracker__hop(source->pid, &event),
                    some(summary)
                    {
                        BString line = BString__sprintf(&bstr("IO-HOP %B"), &summary);
                        TraceBuffer__append(&self->io_records, &line);
                    },
                    none_do_nothing()
                );
                match_optional(
                    IOLatencyTracker__record(&self->io_latency, source->pid, &event),
                    some(summary)
                    {
                        BString line = BString__sprintf(&bstr("IO-LATENCY %B"), &summary);
                        TraceBuffer__append(&self->io_records, &line);
                    },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
        if (!read)
        {
            break;
        }
        source->cursor++;
        drained++;
        work = true;
    }
    return work;
}

static bool TraceService__drain_io(TraceServicePtr self)
{
    bool work = false;
    for_each_fa_ptr(
        TraceSources,
        &self->sources,
        index,
        client,
        {
            match_optional(
                TraceSources__ptr_at(&self->sources, index),
                some(source)
                {
                    work |= TraceSource__drain(self, source);
                },
                none_do_nothing()
            );
        }
    );
    return work;
}

static bool TraceService__reap_io(TraceServicePtr self)
{
    uint64_t now = IpcPlatform__monotonic_usec();
    if (now < self->reap_at_usec)
    {
        return false;
    }
    self->reap_at_usec = now + IO_TRACE_REAP_USEC;
    for_each_fa_ptr(
        TraceSources,
        &self->sources,
        index,
        source,
        {
            bool live = true;
            match_result(
                ProcessRegistry__find(self->process_registry, source.pid),
                success(record)
                {
                    match_optional(
                        record,
                        some(process)
                        {
                            live = true;
                            match_optional(
                                TraceSources__ptr_at(&self->sources, index),
                                some(registered_source)
                                {
                                    registered_source->missing_since_usec = 0;
                                },
                                none_do_nothing()
                            );
                        },
                        none()
                        {
                            match_optional(
                                TraceSources__ptr_at(&self->sources, index),
                                some(registered_source)
                                {
                                    if (registered_source->missing_since_usec == 0)
                                    {
                                        registered_source->missing_since_usec = now;
                                    }
                                    uint64_t missing_usec =
                                        now >= registered_source->missing_since_usec
                                            ? now - registered_source->missing_since_usec
                                            : 0;
                                    live = missing_usec < IO_TRACE_SOURCE_GRACE_USEC;
                                },
                                none_do_nothing()
                            );
                        }
                    );
                },
                error(code, message)
                {
                    return false;
                }
            );
            if (!live)
            {
                TraceSources__remove(&self->sources, source);
                BufferClient__release(&source.buffer);
                return true;
            }
        }
    );
    return false;
}

static bool TraceService__open_io(TraceServicePtr self)
{
    match_result(
        IO__open(&bstr(IO_TRACE_LOG_PATH), IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_TRUNCATE),
        success(handle)
        {
            self->io_log = handle;
            return true;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static bool TraceService__flush_io(TraceServicePtr self)
{
    if (TraceBuffer__is_empty(&self->io_records))
    {
        return false;
    }

    if (self->io_log < 0)
    {
        if (!TraceService__open_io(self))
        {
            return false;
        }
    }

    size_t length = TraceBuffer__length(&self->io_records);
    match_result(
        IO__write_bytes(self->io_log, (const char *)TraceBuffer__data(&self->io_records), length),
        success(written)
        {
            if (written <= 0)
            {
                return false;
            }
            if ((size_t)written < length)
            {
                return TraceBuffer__consume(&self->io_records, (size_t)written);
            }
            uint64_t dropped = TraceBuffer__clear(&self->io_records);
            self->io_write_failure_reported = false;
            if (dropped > 0)
            {
                BString notice = BString__sprintf(&bstr("IO-LATENCY-DROPPED output_records=%llu"), dropped);
                TraceBuffer__append(&self->io_records, &notice);
            }
            return true;
        },
        error(code, message)
        {
            if (!self->io_write_failure_reported)
            {
                kernel_syslog_tagf("hund-verfolgen", "io trace write failed handle=%lld status=%i length=%llu\n",
                                   self->io_log, code, (uint64_t)length);
                self->io_write_failure_reported = true;
            }
            return false;
        }
    );
    return false;
}

static bool TraceService__step(TraceServicePtr self)
{
    QueueParticipantID warden_pid =
        unwrap_optional(ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr("hundekanal-warter")), 0);
    bool work = TraceService__accept_clients(self, warden_pid);
    work |= TraceService__handle_clients(self);
    work |= TraceService__drain_io(self);
    work |= TraceService__reap_io(self);
    work |= TraceService__flush_io(self);
    return work;
}

int main(void)
{
    IOTrace__set_enabled(false);
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    ConstProcessRegistryPtr process_registry;
    match_result(
        process_registry_map(),
        success(mapped)
        {
            process_registry = mapped;
        },
        error(code, message)
        {
            return 1;
        }
    );

    match_result(
        Queue__listen(&bstr(HUND_VERFOLGEN_SERVICE)),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr(HUND_VERFOLGEN_SERVICE))))
            {
                return 1;
            }
            static TraceService service;
            service = TraceService__new(process_registry, arrivals);
            Channel__prepare_wait();
            while (true)
            {
                if (!TraceService__step(&service))
                {
                    Channel__wait_event();
                }
            }
        },
        error(code, message)
        {
            return 1;
        }
    );
    return 1;
}

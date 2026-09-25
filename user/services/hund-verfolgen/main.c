#include "trace_buffer.h"

#include "channel/server.h"
#include "ipc/trace_protocol.h"
#include "services/service_directory.h"
#include "sys/io.h"
#include "sys/service.h"
#include "types/fixed_array.h"

#define TRACE_LOG_PATH "/var/log/driver.log"

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

typedef struct TraceService
{
    QueueArrivalsPtr arrivals;
    TraceClients clients;
    TraceBuffer records;
    IOHandle log;
} TraceService;

typedef TraceService * TraceServicePtr;

static TraceService TraceService__new(QueueArrivalsPtr arrivals)
{
    return (TraceService){
        .arrivals = arrivals,
        .clients = TraceClients__new(),
        .records = TraceBuffer__new(),
        .log = IO_NOT_OPEN,
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

static bool TraceClient__reply(TraceClientPtr self, TraceStatus status)
{
    TraceReply reply = TraceReply__new(status);
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

static TraceStatus TraceService__record(TraceServicePtr self, TraceClientPtr client, ConstTraceRequestPtr request)
{
    if (!TraceRequest__valid_record(request))
    {
        return TRACE_INVALID_REQUEST;
    }
    BString tag = TraceRequest__tag(request);
    BString message = TraceRequest__message(request);
    BString line = BString__sprintf(&bstr("%B pid=%llu: %B"), &tag,
                                    (uint64_t)ChannelServer__client_pid(&client->channel), &message);
    return TraceBuffer__append(&self->records, &line) ? TRACE_OK : TRACE_BUFFER_FULL;
}

static TraceStatus TraceService__enable_storage(TraceServicePtr self)
{
    if (self->log >= 0)
    {
        return TRACE_OK;
    }
    IOHandle log = IO__open(&bstr(TRACE_LOG_PATH), IO_OPEN_WRITE);
    if (log < 0)
    {
        return TRACE_IO_FAILED;
    }
    self->log = log;
    return TRACE_OK;
}

static TraceStatus TraceService__handle_request(TraceServicePtr self, TraceClientPtr client,
                                                ConstTraceRequestPtr request)
{
    if (request->operation == TRACE_OPERATION_RECORD)
    {
        return TraceService__record(self, client, request);
    }
    if (request->operation == TRACE_OPERATION_STORAGE_READY)
    {
        return TraceService__enable_storage(self);
    }
    return TRACE_INVALID_REQUEST;
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
                    TraceStatus status = TRACE_INVALID_REQUEST;
                    if (metadata.size == sizeof(request) &&
                        ChannelServer__read_request(&client->channel, (ChannelDataPtr)&request, sizeof(request)))
                    {
                        status = TraceService__handle_request(self, client, &request);
                    }
                    TraceClient__reply(client, status);
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
                ChannelServer__accept(connection, warden_pid),
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

static bool TraceService__flush(TraceServicePtr self)
{
    if (self->log < 0 || TraceBuffer__is_empty(&self->records))
    {
        return false;
    }

    size_t length = TraceBuffer__length(&self->records);
    ByteCount written = IO__write_bytes(self->log, (const char *)TraceBuffer__data(&self->records), length);
    if (written <= 0)
    {
        return false;
    }

    if ((size_t)written < length)
    {
        return TraceBuffer__consume(&self->records, (size_t)written);
    }

    uint64_t dropped = TraceBuffer__clear(&self->records);
    if (dropped > 0)
    {
        BString notice = BString__sprintf(&bstr("HUND-VERFOLGEN pid=0: dropped %llu driver trace records"), dropped);
        TraceBuffer__append(&self->records, &notice);
    }
    return true;
}

static bool TraceService__step(TraceServicePtr self)
{
    QueueParticipantID warden_pid =
        unwrap_optional(ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &bstr("hundekanal-warter")), 0);
    bool work = TraceService__accept_clients(self, warden_pid);
    work |= TraceService__handle_clients(self);
    work |= TraceService__flush(self);
    return work;
}

int main(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }

    match_result(
        Queue__listen(&bstr(HUND_VERFOLGEN_SERVICE)),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr(HUND_VERFOLGEN_SERVICE))))
            {
                return 1;
            }
            static TraceService service;
            service = TraceService__new(arrivals);
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

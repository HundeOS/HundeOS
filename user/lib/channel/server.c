#include "server.h"

#include "ipc/ipc_platform.h"
#include "trace/io_trace.h"

static BString channel_server_invalid_request = bstr("Invalid channel server request");
static BString channel_server_transport_failed = bstr("Channel server transport failed");
static BString channel_server_wrong_warden = bstr("Channel connection did not come from the warden");

static ChannelServer ChannelServer__new(QueuePtr control)
{
    return (ChannelServer){.control = *control};
}

static ChannelServerRequest ChannelServerRequest__new(size_t size, QueueParticipantID client_pid)
{
    return (ChannelServerRequest){.size = size, .client_pid = client_pid};
}

Result(ChannelServer) ChannelServer__accept(QueueConnectionID connection, QueueParticipantID warden_pid)
{
    match_result(
        Queue__accept(connection),
        success(control)
        {
            if (warden_pid == 0 || Queue__peer_pid(&control) != warden_pid)
            {
                QueueConnection__close(connection);
                return Error(ChannelServer, channel_server_wrong_warden, CHANNEL_TRANSPORT_FAILED);
            }
            return Success(ChannelServer, ChannelServer__new(&control));
        },
        error(code, message)
        {
            return Error(ChannelServer, channel_server_transport_failed, code);
        }
    );
    return Error(ChannelServer, channel_server_transport_failed, CHANNEL_TRANSPORT_FAILED);
}

QueueConnectionID ChannelServer__connection(ChannelServerPtr self)
{
    return Queue__connection(&self->control);
}

QueueParticipantID ChannelServer__client_pid(ChannelServerPtr self)
{
    return self->client_pid;
}

QueueParticipantID ChannelServer__transport_pid(ChannelServerPtr self)
{
    return self->transport_pid;
}

QueueCode ChannelServer__status(ChannelServerPtr self)
{
    return self->closed ? QUEUE_CLOSED : Queue__status(&self->control);
}

Result(OptionalChannelServerRequest) ChannelServer__poll(ChannelServerPtr self)
{
    if (self->handling_request)
    {
        match_result(
            Queue__pop(&self->control),
            success(message)
            {
                RequestTicket ticket = ChannelControlMessage__ticket(&message);
                if (message.kind == CHANNEL_REQUEST_CANCEL && RequestTicket__equals(&self->ticket, &ticket))
                {
                    return Error(OptionalChannelServerRequest, bstr("Channel client disconnected"), QUEUE_CLOSED);
                }
                return Error(OptionalChannelServerRequest, channel_server_invalid_request, CHANNEL_INVALID_REPLY);
            },
            error(code, message)
            {
                if (code == QUEUE_EMPTY || code == QUEUE_PENDING)
                {
                    return Success(OptionalChannelServerRequest, None(ChannelServerRequest));
                }
                return Error(OptionalChannelServerRequest, message, code);
            }
        );
    }
    match_result(
        Queue__pop(&self->control),
        success(message)
        {
            RequestTicket ticket = ChannelControlMessage__ticket(&message);
            if (message.kind != CHANNEL_REQUEST_SERVE || message.arg0 == 0 ||
                message.arg1 >= CHANNEL_PAYLOAD_CAPACITY || message.arg2 == 0 || message.arg3 == 0 ||
                !RequestTicket__valid(&ticket) ||
                (self->ticket.connection == ticket.connection &&
                 self->ticket.metadata.generation >= ticket.metadata.generation))
            {
                return Error(OptionalChannelServerRequest, channel_server_invalid_request, CHANNEL_INVALID_REPLY);
            }
            self->payload = (ChannelPayloadPtr)message.arg0;
            self->request_size = message.arg1;
            self->token = message.arg2;
            self->client_pid = ticket.metadata.origin_pid;
            self->transport_pid = message.arg3;
            self->handling_request = true;
            self->ticket = ticket;
            uint64_t received_at_usec = IpcPlatform__monotonic_usec();
            IOTrace__record_with_peer(IO_TRACE_SERVER_RECEIVE, &self->ticket, received_at_usec, self->transport_pid,
                                      ChannelPayload__request_published_at(self->payload));
            return Success(OptionalChannelServerRequest,
                           Some(ChannelServerRequest, ChannelServerRequest__new(self->request_size, self->client_pid)));
        },
        error(code, message)
        {
            if (code == QUEUE_EMPTY || code == QUEUE_PENDING)
            {
                return Success(OptionalChannelServerRequest, None(ChannelServerRequest));
            }
            return Error(OptionalChannelServerRequest, channel_server_transport_failed, code);
        }
    );
    return Error(OptionalChannelServerRequest, channel_server_transport_failed, CHANNEL_TRANSPORT_FAILED);
}

bool ChannelServer__read_request(ChannelServerPtr self, ChannelDataPtr request, size_t size)
{
    return self->handling_request && size == self->request_size &&
           ChannelPayload__read_request(self->payload, request, size);
}

Result(ChannelCode) ChannelServer__reply(ChannelServerPtr self, ConstChannelDataPtr reply, size_t size)
{
    if (!self->handling_request || size >= CHANNEL_PAYLOAD_CAPACITY ||
        !ChannelPayload__write_reply(self->payload, reply, size))
    {
        return Error(ChannelCode, channel_server_invalid_request, CHANNEL_INVALID_REPLY);
    }
    QueueMessage response = ChannelControlMessage__new(CHANNEL_RESPONSE_REPLIED, 0, size, self->token);
    response.request = self->ticket.metadata;
    uint64_t replied_at_usec = IpcPlatform__monotonic_usec();
    match_result(
        Queue__push(&self->control, &response),
        success(code)
        {
            IOTrace__record(IO_TRACE_SERVER_REPLY, &self->ticket, replied_at_usec);
            self->handling_request = false;
            return Success(ChannelCode, CHANNEL_OK);
        },
        error(code, message)
        {
            if (code == QUEUE_FULL)
            {
                return Error(ChannelCode, channel_server_transport_failed, CHANNEL_PENDING);
            }
            return Error(ChannelCode, channel_server_transport_failed, CHANNEL_TRANSPORT_FAILED);
        }
    );
    return Error(ChannelCode, channel_server_transport_failed, CHANNEL_TRANSPORT_FAILED);
}

ChannelCode ChannelServer__close(ChannelServerPtr self)
{
    if (self->closed)
    {
        return CHANNEL_OK;
    }
    if (QueueConnection__close(ChannelServer__connection(self)) != QUEUE_OK)
    {
        return CHANNEL_TRANSPORT_FAILED;
    }
    self->closed = true;
    return CHANNEL_OK;
}

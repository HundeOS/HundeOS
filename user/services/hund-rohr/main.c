#include "pipe.h"

#include "io/server.h"
#include "sys/service.h"

static Optional(IOReply)
    PipeServer__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    PipeServicePtr self = context;
    IOReply reply = PipeService__execute(self, request);
    return reply.status == IO_WAIT_FOR_DATA ? None(IOReply) : Some(IOReply, reply);
}

static bool PipeServer__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    PipeService__release(context, session);
    return true;
}

int main(void)
{
    static PipeService pipes;
    static IOService service;
    pipes = PipeService__new();
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    if (IOService__listen_into(&service, &bstr(IO_PIPE_SERVICE), &pipes, PipeServer__execute, PipeServer__disconnect) !=
        IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(IO_PIPE_SERVICE))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}

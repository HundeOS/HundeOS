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
    ServiceCode become_status = service_become_service();
    if (!service_ok(become_status))
    {
        return 1;
    }
    IOStatus listen_status =
        IOService__listen_into(&service, &bstr(IO_PIPE_SERVICE), &pipes, PipeServer__execute, PipeServer__disconnect);
    if (listen_status != IO_OK)
    {
        return 1;
    }
    ServiceCode ready_status = service_publish_ready(&bstr(IO_PIPE_SERVICE));
    if (!service_ok(ready_status))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        bool progress = IOService__poll(&service);
        if (!progress)
        {
            Channel__wait_event();
        }
    }
}

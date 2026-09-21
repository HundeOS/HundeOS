#include "stream_runtime.h"
#include <setjmp.h>
static jmp_buf completed;
static IOStreamDriverPtr active_driver;
int StreamDriverTest__run(int (*entry)(void))
{
    if (setjmp(completed) == 0)
    {
        return entry();
    }
    return 0;
}
void StreamDriverTest__stop(void)
{
    longjmp(completed, 1);
}
IOStreamDriver IOStreamDriver__new(Flags access, IOServiceContext context, IOStreamTransfer transfer)
{
    return (IOStreamDriver){.access = access, .context = context, .transfer = transfer};
}
Optional(IOReply)
    IOStreamDriver__execute(IOServiceContext context, QueueConnectionID session, uint64_t pid, IORequestPtr request)
{
    return None(IOReply);
}
bool IOStreamDriver__disconnect(IOServiceContext context, QueueConnectionID session, uint64_t pid)
{
    return true;
}
Result(IOService) IOService__listen(ConstBStringPtr name, IOServiceContext context, IOServiceExecute execute,
                                    IOServiceDisconnect disconnect)
{
    active_driver = context;
    return Success(IOService, ((IOService){0}));
}

IOStatus IOService__listen_into(IOServicePtr self, ConstBStringPtr name, IOServiceContext context,
                                IOServiceExecute execute, IOServiceDisconnect disconnect)
{
    active_driver = context;
    *self = (IOService){0};
    return IO_OK;
}

bool IOService__poll(IOServicePtr self)
{
    StreamDriverTest__step(active_driver);
    return true;
}
void Channel__prepare_wait(void)
{
}
void Channel__wait_event(void)
{
}
ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}

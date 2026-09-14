#ifndef HUND_IO_PROTOCOL_SERVER_H
#define HUND_IO_PROTOCOL_SERVER_H
#include "protocol.h"
#include "channel/server.h"
#include "types/fixed_array.h"

typedef void * IOServiceContext;
typedef Optional(IOReply) (*IOServiceExecute)(IOServiceContext context, QueueConnectionID session, uint64_t pid,
                                              IORequestPtr request);
typedef bool (*IOServiceDisconnect)(IOServiceContext context, QueueConnectionID session, uint64_t pid);

typedef enum IOServicePhase
{
    IO_SERVICE_RECEIVE,
    IO_SERVICE_EXECUTE,
    IO_SERVICE_REPLY
} IOServicePhase;
typedef struct IOServiceConnection
{
    ChannelServer channel;
    IORequest request;
    IOReply reply;
    IOServicePhase phase;
} IOServiceConnection;
typedef IOServiceConnection * IOServiceConnectionPtr;
OptionalDef(IOServiceConnection);
bool IOServiceConnection__equals(void * first, void * second);
FixedArrayDefine(IOServiceConnection, QUEUE_LIMIT, IOServiceConnections, IOServiceConnection__equals);

typedef struct IOService
{
    QueueArrivalsPtr arrivals;
    IOServiceConnections connections;
    IOServiceContext context;
    IOServiceExecute execute;
    IOServiceDisconnect disconnect;
} IOService;
typedef IOService * IOServicePtr;
ResultDef(IOService);
Result(IOService) IOService__listen(ConstBStringPtr name, IOServiceContext context, IOServiceExecute execute,
                                    IOServiceDisconnect disconnect);
bool IOService__poll(IOServicePtr self);
#endif

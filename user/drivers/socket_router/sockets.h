#ifndef HUND_SOCKET_SERVICE_H
#define HUND_SOCKET_SERVICE_H
#include "io/protocol.h"
#include "types/fixed_array.h"

typedef uint8_t SocketByte;
OptionalDef(SocketByte);
bool SocketByte__equals(void * first, void * second);
FixedArrayDefine(SocketByte, 1024, SocketBytes, SocketByte__equals);
typedef uint64_t SocketID;
OptionalDef(SocketID);
bool SocketID__equals(void * first, void * second);
FixedArrayDefine(SocketID, 16, SocketPending, SocketID__equals);
typedef enum SocketKind
{
    SOCKET_LISTENER,
    SOCKET_CONNECTION
} SocketKind;
typedef struct SocketResource
{
    SocketID id;
    QueueConnectionID session;
    uint64_t pid;
    SocketKind kind;
    BString name;
    Optional(SocketID) peer;
    SocketPending pending;
    SocketBytes bytes;
} SocketResource;
typedef SocketResource * SocketResourcePtr;
OptionalDef(SocketResource);
OptionalDef(SocketResourcePtr);
bool SocketResource__equals(void * first, void * second);
FixedArrayDefine(SocketResource, 64, SocketResources, SocketResource__equals);
typedef struct SocketService
{
    SocketResources resources;
    SocketID next_id;
} SocketService;
typedef SocketService * SocketServicePtr;
SocketService SocketService__new(void);
IOReply SocketService__execute(SocketServicePtr self, IORequestPtr request);
void SocketService__release(SocketServicePtr self, QueueConnectionID session);
#endif

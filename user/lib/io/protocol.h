#ifndef HUND_IO_PROTOCOL_H
#define HUND_IO_PROTOCOL_H

#include "channel/channel.h"
#include "syscalls/io.h"
#include "syscalls/socket.h"

#define IO_SYSTEM_SERVICE "hund-io-system"
#define IO_SOCKET_SERVICE "socket_router"
#define IO_FILESYSTEM_SERVICE "hund-dateisystem"
#define IO_PROTOCOL_DATA_SIZE 256U
#define IO_SESSION_HANDLE_BASE (1LL << 32)
#define IO_FILESYSTEM_HANDLE_BASE (1LL << 40)

typedef enum IORequestOperation
{
    IO_REQUEST_OPEN = 1,
    IO_REQUEST_ACCEPT,
    IO_REQUEST_READ,
    IO_REQUEST_WRITE,
    IO_REQUEST_CLOSE,
    IO_REQUEST_RELEASE_CLIENT,
    IO_REQUEST_LIST_DIR,
    IO_REQUEST_MKDIR,
    IO_REQUEST_REMOVE,
} IORequestOperation;

typedef struct IORequest
{
    IORequestOperation operation;
    IOHandle handle;
    Flags flags;
    uint64_t count;
    uint64_t offset;
    uint64_t client_pid;
    QueueConnectionID client_session;
    uint8_t data[IO_PROTOCOL_DATA_SIZE];
} IORequest;
typedef IORequest * IORequestPtr;

typedef struct IOReply
{
    IOStatus status;
    int64_t value;
    uint8_t data[IO_PROTOCOL_DATA_SIZE];
    DirectoryEntry entry;
} IOReply;
typedef IOReply * IOReplyPtr;
OptionalDef(IOReply);

static inline IORequest IORequest__new(IORequestOperation operation, IOHandle handle)
{
    return (IORequest){.operation = operation, .handle = handle};
}

static inline BString IORequest__path(IORequestPtr self)
{
    return BString__from_chars((const char *)self->data, self->count <= IO_PROTOCOL_DATA_SIZE ? self->count : 0);
}

static inline IOReply IOReply__new(IOStatus status, int64_t value)
{
    return (IOReply){.status = status, .value = value};
}

_Static_assert(sizeof(IORequest) < CHANNEL_PAYLOAD_CAPACITY, "IO request exceeds channel payload");
_Static_assert(sizeof(IOReply) < CHANNEL_PAYLOAD_CAPACITY, "IO reply exceeds channel payload");
#endif

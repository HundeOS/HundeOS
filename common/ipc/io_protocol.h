#ifndef HUND_IO_PROTOCOL_H
#define HUND_IO_PROTOCOL_H

#include "channel.h"
#include "buffer.h"
#include "syscalls/io.h"
#include "syscalls/socket.h"
#include "syscalls/types.h"

#define IO_SYSTEM_SERVICE "hund-io-system"
#define IO_SOCKET_SERVICE "socket_router"
#define IO_PIPE_SERVICE "hund-rohr"
#define IO_FILESYSTEM_SERVICE "hund-dateisystem"
#define IO_PROTOCOL_DATA_SIZE 256U
#define IO_SESSION_HANDLE_BASE (1LL << 32)
#define IO_FILESYSTEM_HANDLE_BASE (1LL << 40)
#define IO_STREAM_HANDLE_BASE (1LL << 41)
#define IO_UART_RX_SERVICE "uart_rx"
#define IO_UART_TX_SERVICE "uart_tx"
#define IO_VIDEO_SERVICE "video_term"
#define IO_KEYBOARD_SERVICE "keyboard"
#define IO_DIRECTORY_BATCH_CAPACITY 3U
#define IO_ROUTE_LIMIT 8U

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
    IO_REQUEST_MOUNT,
    IO_REQUEST_LINK,
    IO_REQUEST_RELEASE_BUFFERS,
    IO_REQUEST_SET_FOREGROUND_PROCESS,
    IO_REQUEST_FILE_INFO,
    IO_REQUEST_SEEK,
    IO_REQUEST_WATCH_FILE,
    IO_REQUEST_WAIT_FILE_CHANGE,
    IO_REQUEST_PIPE_CREATE,
    IO_REQUEST_GET_TERMINAL_ATTRIBUTES,
    IO_REQUEST_SET_TERMINAL_ATTRIBUTES,
    IO_REQUEST_INHERIT_PREPARE,
    IO_REQUEST_INHERIT_COMMIT,
    IO_REQUEST_INHERIT_CLAIM,
    IO_REQUEST_INHERIT_ABORT,
} IORequestOperation;

static inline IOFileChange IOFileChange__new(IOHandle handle, uint64_t generation, Flags events)
{
    return (IOFileChange){.handle = handle, .generation = generation, .events = events};
}

typedef struct IORequest
{
    IORequestOperation operation;
    IOHandle handle;
    Flags flags;
    uint64_t count;
    uint64_t offset;
    uint64_t client_pid;
    QueueConnectionID client_session;
    uint64_t foreground_pid;
    uint64_t inheritance_token;
    uint64_t inheritance_pid;
    IOTerminalAttributes terminal_attributes;
    bool buffered;
    BufferID buffer;
    // Receiver-local address: never trusted from a channel message.
    Address transfer_address;
    uint8_t data[IO_PROTOCOL_DATA_SIZE];
    uint64_t target_path_length;
    uint8_t target_path_data[IO_PROTOCOL_DATA_SIZE];
} IORequest;

typedef IORequest * IORequestPtr;

typedef struct IODirectoryBatch
{
    DirectoryEntry entries[IO_DIRECTORY_BATCH_CAPACITY];
    uint64_t count;
} IODirectoryBatch;

typedef struct IORoute
{
    bool final;
    uint64_t service_length;
    uint8_t service[IO_PROTOCOL_DATA_SIZE];
    uint64_t path_length;
    uint8_t path[IO_PROTOCOL_DATA_SIZE];
    uint64_t target_path_length;
    uint8_t target_path[IO_PROTOCOL_DATA_SIZE];
} IORoute;

typedef IORoute * IORoutePtr;
typedef const IORoute * ConstIORoutePtr;

static inline IORoute IORoute__new(ConstBStringPtr service, ConstBStringPtr path, ConstBStringPtr target_path,
                                   bool final)
{
    IORoute route = {.final = final};
    route.service_length = BString__copy_to(service, (char *)route.service, sizeof(route.service));
    route.path_length = BString__copy_to(path, (char *)route.path, sizeof(route.path));
    route.target_path_length = BString__copy_to(target_path, (char *)route.target_path, sizeof(route.target_path));
    return route;
}

static inline BString IORoute__service(ConstIORoutePtr self)
{
    return BString__from_chars((const char *)self->service,
                               self->service_length < IO_PROTOCOL_DATA_SIZE ? self->service_length : 0);
}

static inline BString IORoute__path(ConstIORoutePtr self)
{
    return BString__from_chars((const char *)self->path,
                               self->path_length < IO_PROTOCOL_DATA_SIZE ? self->path_length : 0);
}

static inline BString IORoute__target_path(ConstIORoutePtr self)
{
    return BString__from_chars((const char *)self->target_path,
                               self->target_path_length < IO_PROTOCOL_DATA_SIZE ? self->target_path_length : 0);
}

typedef struct IOReply
{
    IOStatus status;
    Anything value;
    IOPipe pipe;
    BufferPair buffers;
    Flags inheritance_flags;

    union
    {
        uint8_t data[IO_PROTOCOL_DATA_SIZE];
        IODirectoryBatch directory;
        IOFileInfo file_info;
        IOFileChange file_change;
        IOTerminalAttributes terminal_attributes;
        IORoute route;
    };
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

static inline BString IORequest__target_path(IORequestPtr self)
{
    return BString__from_chars((const char *)self->target_path_data,
                               self->target_path_length < IO_PROTOCOL_DATA_SIZE ? self->target_path_length : 0);
}

static inline void IORequest__set_target_path(IORequestPtr self, ConstBStringPtr path)
{
    self->target_path_length = BString__copy_to(path, (char *)self->target_path_data, sizeof(self->target_path_data));
}

static inline IOReply IOReply__new(IOStatus status, Anything value)
{
    return (IOReply){.status = status, .value = value};
}

static inline IOReply IOReply__redirect(ConstBStringPtr service, ConstBStringPtr path, ConstBStringPtr target_path,
                                        bool final)
{
    IOReply reply = IOReply__new(IO_REDIRECT, 0);
    reply.route = IORoute__new(service, path, target_path, final);
    return reply;
}

static inline uint64_t IORequest__capacity(IORequestPtr self)
{
    bool transfer = self->operation == IO_REQUEST_READ || self->operation == IO_REQUEST_WRITE;
    return transfer && self->transfer_address != 0 ? BUFFER_SIZE : IO_PROTOCOL_DATA_SIZE;
}

static inline ChannelDataPtr IORequest__read_target(IORequestPtr self, IOReplyPtr reply)
{
    return self->transfer_address != 0 ? (ChannelDataPtr)self->transfer_address : reply->data;
}

static inline ConstChannelDataPtr IORequest__write_source(IORequestPtr self)
{
    return self->transfer_address != 0 ? (ConstChannelDataPtr)self->transfer_address : self->data;
}

static inline IOFileWatch * IORequest__file_watches(IORequestPtr self)
{
    return (IOFileWatch *)self->data;
}

_Static_assert(sizeof(IORequest) < CHANNEL_PAYLOAD_CAPACITY, "IO request exceeds channel payload");
_Static_assert(sizeof(IOReply) < CHANNEL_PAYLOAD_CAPACITY, "IO reply exceeds channel payload");
#endif

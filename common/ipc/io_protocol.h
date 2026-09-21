#ifndef HUND_IO_PROTOCOL_H
#define HUND_IO_PROTOCOL_H

#include "channel.h"
#include "buffer.h"
#include "syscalls/io.h"
#include "syscalls/socket.h"

#define IO_SYSTEM_SERVICE "hund-io-system"
#define IO_SOCKET_SERVICE "socket_router"
#define IO_FILESYSTEM_SERVICE "hund-dateisystem"
#define IO_PROTOCOL_DATA_SIZE 256U
#define IO_SESSION_HANDLE_BASE (1LL << 32)
#define IO_FILESYSTEM_HANDLE_BASE (1LL << 40)
#define IO_STREAM_HANDLE_BASE (1LL << 41)
#define IO_UART_RX_SERVICE "uart_rx"
#define IO_UART_TX_SERVICE "uart_tx"
#define IO_VIDEO_SERVICE "video_term"
#define IO_KEYBOARD_SERVICE "keyboard"

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
    bool buffered;
    BufferID buffer;
    // Receiver-local address: never trusted from a channel message.
    Address transfer_address;
    uint8_t data[IO_PROTOCOL_DATA_SIZE];
    uint64_t target_path_length;
    uint8_t target_path_data[IO_PROTOCOL_DATA_SIZE];
} IORequest;
typedef IORequest * IORequestPtr;

typedef struct IOReply
{
    IOStatus status;
    int64_t value;
    uint8_t data[IO_PROTOCOL_DATA_SIZE];
    DirectoryEntry entry;
    BufferPair buffers;
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

static inline IOReply IOReply__new(IOStatus status, int64_t value)
{
    return (IOReply){.status = status, .value = value};
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

_Static_assert(sizeof(IORequest) < CHANNEL_PAYLOAD_CAPACITY, "IO request exceeds channel payload");
_Static_assert(sizeof(IOReply) < CHANNEL_PAYLOAD_CAPACITY, "IO reply exceeds channel payload");
#endif

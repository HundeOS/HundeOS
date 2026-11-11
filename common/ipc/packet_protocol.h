#ifndef HUND_PACKET_PROTOCOL_H
#define HUND_PACKET_PROTOCOL_H

#include "buffer.h"

typedef enum PacketProtocolVersion
{
    PACKET_PROTOCOL_VERSION = 1,
} PacketProtocolVersion;

typedef enum PacketProtocolOperation
{
    PACKET_OPEN_RX = 1,
    PACKET_OPEN_TX,
    PACKET_TRANSMIT,
    PACKET_RECEIVE,
    PACKET_CLOSE,
} PacketProtocolOperation;

typedef enum PacketProtocolStatus
{
    PACKET_OK = 0,
    PACKET_INVALID = -1,
    PACKET_BUSY = -2,
    PACKET_CLOSED = -3,
    PACKET_LINK_DOWN = -4,
    PACKET_IO_ERROR = -5,
    PACKET_DENIED = -6,
} PacketProtocolStatus;

typedef enum PacketProtocolLimit
{
    PACKET_ETHERNET_HEADER_SIZE = 14,
    PACKET_FRAME_CAPACITY = 1514,
    PACKET_QUEUE_CAPACITY = 8,
    PACKET_SESSION_LIMIT = 16,
} PacketProtocolLimit;

typedef struct PacketRequest
{
    uint32_t version;
    PacketProtocolOperation operation;
    BufferID buffer;
    uint64_t offset;
    uint64_t length;
} PacketRequest;

typedef struct PacketResponse
{
    uint32_t version;
    PacketProtocolStatus status;
    uint64_t length;
    BufferDescriptor buffer;
    uint32_t mtu;
    uint32_t link_up;
    uint8_t mac[6];
    uint8_t reserved[2];
} PacketResponse;

_Static_assert(sizeof(PacketRequest) == 32, "Packet request ABI changed");
_Static_assert(sizeof(PacketResponse) == 64, "Packet response ABI changed");
_Static_assert(offsetof(PacketResponse, buffer) == 16, "Packet buffer ABI changed");
_Static_assert(sizeof(PacketRequest) < CHANNEL_PAYLOAD_CAPACITY, "Packet request exceeds Channel payload");
_Static_assert(sizeof(PacketResponse) < CHANNEL_PAYLOAD_CAPACITY, "Packet response exceeds Channel payload");

#endif

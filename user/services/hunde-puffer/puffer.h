#ifndef HUNDE_PUFFER_H
#define HUNDE_PUFFER_H
#include "buffer/client.h"
#include "ipc/kernel_notification_queue.h"
#include "types/fixed_array.h"
typedef struct BufferLease
{
    BufferID id;
    BufferParticipant client;
    BufferParticipant server;
    BString service;
    bool pooled;
} BufferLease;
typedef BufferLease * BufferLeasePtr;
OptionalDef(BufferLease);
bool BufferLease__equals(void * first, void * second);
FixedArrayDefine(BufferLease, BUFFER_LIMIT, BufferLeases, BufferLease__equals);
typedef struct Puffer
{
    BufferLeases leases;
    ServiceDirectoryPtr directory;
} Puffer;
typedef Puffer * PufferPtr;
Puffer Puffer__new(ServiceDirectoryPtr directory);
BufferReply Puffer__execute(PufferPtr self, BufferParticipant caller, ConstBufferRequestPtr request);
void Puffer__reconcile(PufferPtr self, BufferID id);
void Puffer__housekeeping(PufferPtr self);
#endif

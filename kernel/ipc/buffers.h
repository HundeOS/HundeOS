#ifndef KERNEL_BUFFERS_H
#define KERNEL_BUFFERS_H
#include "../../common/ipc/buffer.h"
#include "../../common/ipc/kernel_notification_queue.h"
#include "../process.h"
#include "../../common/process/process_registry.h"

_Static_assert(PROCESS_REGISTRY_VADDR + PROCESS_REGISTRY_SIZE <= BUFFER_EVENTS_ADDRESS,
               "Buffer events overlap process registry");
_Static_assert(BUFFER_EVENTS_ADDRESS + KERNEL_NOTIFICATION_QUEUE_PAGE_SIZE <= BUFFER_BASE,
               "Buffer events overlap data buffers");
_Static_assert(BUFFER_LIMIT <= KERNEL_NOTIFICATION_QUEUE_CAPACITY, "Every buffer needs a notification slot");

typedef struct ManagedBuffer
{
    UserAddress memory;
    BufferID id;
    BufferParticipant client;
    BufferParticipant server;
    BufferDirection direction;
    bool retiring;
    bool retained;
} ManagedBuffer;
typedef ManagedBuffer * ManagedBufferPtr;
OptionalDef(ManagedBuffer);
OptionalDef(ManagedBufferPtr);
bool ManagedBuffer__equals(void * first, void * second);
FixedArrayDefine(ManagedBuffer, BUFFER_LIMIT, ManagedBuffers, ManagedBuffer__equals);
typedef struct Buffers
{
    ManagedBuffers allocations;
    BufferID generation;
    BufferParticipant manager;
    UserAddress notifications;
} Buffers;
typedef Buffers * BuffersPtr;
typedef struct Scheduler * SchedulerPtr;
// Caller holds the scheduler lock; never wait or switch context inside these operations.
Result(Address) Buffers__operation(BuffersPtr self, SchedulerPtr scheduler, ProcessPtr caller, uint64_t operation,
                                   uint64_t first, uint64_t second, uint64_t third, uint64_t fourth);
void Buffers__exit(BuffersPtr self, SchedulerPtr scheduler, ProcessPtr process);
bool Buffers__has_pending_notifications(BuffersPtr self, ProcessPtr process);
Result(BufferStatus) Buffers__drop(BuffersPtr self, SchedulerPtr scheduler, BufferID id, BufferParticipant participant);
Result(Address) Buffers__kernel_read(BuffersPtr self, BufferID id, BufferParticipant server, uint64_t size);
#endif

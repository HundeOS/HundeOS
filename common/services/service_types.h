#ifndef HUND_SERVICE_TYPES_H
#define HUND_SERVICE_TYPES_H

#include <stdint.h>

#include "../types/optional.h"
#include "../types/result.h"

typedef uint64_t AtomicMessageQueueHandleID;
typedef AtomicMessageQueueHandleID AtomicMessageQueueConnectionID;
typedef AtomicMessageQueueHandleID AtomicMessageQueueEndpointID;
typedef uint64_t AtomicMessageQueueParticipantID;
typedef uint64_t AtomicMessageQueueEpoch;
typedef uint64_t AtomicMessageQueueCursor;
typedef uint64_t AtomicMessageQueueMessageCount;
typedef uint64_t AtomicMessageQueueSlotIndex;
typedef uint64_t AtomicMessageQueueStateFlag;
typedef uint64_t AtomicMessageQueueHandleGeneration;

OptionalDef(AtomicMessageQueueConnectionID);
OptionalDef(AtomicMessageQueueEndpointID);
ResultDef(AtomicMessageQueueConnectionID);
ResultDef(AtomicMessageQueueHandleID);

#endif

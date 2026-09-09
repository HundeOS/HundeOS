#ifndef __KERNNEL_SCHEDULER_PARKED_H
#define __KERNNEL_SCHEDULER_PARKED_H

#include <stdint.h>

#include "../types/result.h"
#include "../types/optional.h"
#include "../types/bstring.h"
#include "../types/linked_list.h"
#include "../process.h"
#include "kernel_context.h"
#include "types.h"

LinkedListDefine(ParkedOperationPtr, BlockedParkedOperations, &Ptr__equals);

struct ParkedOperation
{
    KernelContextPtr kernel_context;
    bool owns_kernel_context;
    bool ready;
    Optional(Continuation) continuation;
    Optional(OperationKey) operation_key;
    ParkReason park_reason;
    Optional(ReturnCode) return_code;
    // Owned dependencies that must resolve before this operation can continue.
    BlockedParkedOperations blocked_operations;
    uint64_t pending_operations;
};

OptionalDef(ParkedOperation);

ParkedOperationPtr ParkedOperation__heap_new(KernelContextPtr kernel_context, OptionalPtr(Continuation) continuation,
                                             ParkReason park_reason);
ParkedOperationPtr ParkedOperation__heap_new_with_context_ownership(KernelContextPtr kernel_context,
                                                                    OptionalPtr(Continuation) continuation,
                                                                    ParkReason park_reason, bool owns_kernel_context);
bool ParkedOperation__is_ready(ParkedOperationPtr self);
void ParkedOperation__mark_ready(ParkedOperationPtr self);
Result(CollectionCode)
    ParkedOperation__add_blocked_operation(ParkedOperationPtr self, ParkedOperationPtr blocked_operation);
Result(CollectionCode)
    ParkedOperation__resolve_blocked_operation(ParkedOperationPtr self, ParkedOperationPtr blocked_operation);

BString ParkReason__to_string(ParkReason reason);

void ParkedOperation__dispose(ParkedOperationPtr self);

#endif

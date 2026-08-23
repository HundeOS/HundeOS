#include "parked.h"
#include "../debug.h"
#include "../kalloc.h"

LinkedListBodyDefine(BlockedParkedOperations);

ParkedOperationPtr ParkedOperation__heap_new(
    KernelContextPtr kernel_context, Optional(Continuation) continuation, ParkReason park_reason
)
{
    return ParkedOperation__heap_new_with_context_ownership(
        kernel_context, continuation, park_reason, false
    );
}

ParkedOperationPtr ParkedOperation__heap_new_with_context_ownership(
    KernelContextPtr kernel_context, Optional(Continuation) continuation,
    ParkReason park_reason, bool owns_kernel_context
)
{
    ParkedOperationPtr context = ptr(Kernel__alloc_kernel(sizeof(ParkedOperation)), ParkedOperation);
    *context = (ParkedOperation) {
        .kernel_context = kernel_context,
        .owns_kernel_context = owns_kernel_context,
        .ready = false,
        .continuation = continuation,
        .operation_key = None(OperationKey),
        .park_reason = park_reason,
        .blocked_operations = BlockedParkedOperations__new(),
        .pending_operations = 0,
        .return_code = None(ReturnCode),
    };

    return context;
}

bool ParkedOperation__is_ready(ParkedOperationPtr self)
{
    return self->ready && self->pending_operations == 0;
}

void ParkedOperation__mark_ready(ParkedOperationPtr self)
{
    self->ready = true;
}

Result(CollectionCode) ParkedOperation__add_blocked_operation(
    ParkedOperationPtr self, ParkedOperationPtr blocked_operation
)
{
    match_result(CollectionCode, BlockedParkedOperations__push(&self->blocked_operations, blocked_operation),
        success(index)
        {
            self->pending_operations++;
            return Success(CollectionCode, index);
        },
        error(code, message) { return Error(CollectionCode, message, code); }
    )

    return Error(CollectionCode, bstring("Unable to add blocked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Result(CollectionCode) ParkedOperation__resolve_blocked_operation(
    ParkedOperationPtr self, ParkedOperationPtr blocked_operation
)
{
    match_result(CollectionCode, BlockedParkedOperations__remove(&self->blocked_operations, blocked_operation),
        success(index)
        {
            if (self->pending_operations > 0)
            {
                self->pending_operations--;
            }
            return Success(CollectionCode, index);
        },
        error(code, message) { return Error(CollectionCode, message, code); }
    )

    return Error(CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND);
}

BString ParkReason__to_string(ParkReason reason)
{
    switch (reason)
    {
        case PARK_WAIT_CHILD:
            return BSTRING_NEW("PARK_WAIT_CHILD");
        case PARK_WAIT_IO:
            return BSTRING_NEW("PARK_WAIT_IO");
        case PARK_WAIT_SERVICE_EVENT:
            return BSTRING_NEW("PARK_WAIT_SERVICE_EVENT");
        case PARK_WAIT_CHILD_EVENT:
            return BSTRING_NEW("PARK_WAIT_CHILD_EVENT");
        case PARK_WAIT_SERVICE_READY:
            return BSTRING_NEW("PARK_WAIT_SERVICE_READY");
        default:
            return BSTRING_NEW("PARK_WAIT_UNKNOWN");
    }
}

void ParkedOperation__dispose(ParkedOperationPtr self)
{
    while (BlockedParkedOperations__len(&self->blocked_operations) > 0)
    {
        match_optional(ParkedOperationPtr, BlockedParkedOperations__remove_at(&self->blocked_operations, 0),
            some(blocked_operation) { ParkedOperation__dispose(blocked_operation); },
            none() {}
        )
    }
    BlockedParkedOperations__dispose(&self->blocked_operations);
    if (self->owns_kernel_context)
    {
        KernelContext__dispose_heap(self->kernel_context);
    }
    Kernel__free(addr(self));
}

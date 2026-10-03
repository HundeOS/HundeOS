#include "parked.h"
#include "../debug.h"
#include "../kalloc.h"

LinkedListBodyDefine(BlockedParkedOperations);

static ParkedOperation ParkedOperation__new(KernelContextPtr kernel_context, OptionalPtr(Continuation) continuation,
                                            ParkReason park_reason, bool owns_kernel_context)
{
    return (ParkedOperation){
        .kernel_context = kernel_context,
        .owns_kernel_context = owns_kernel_context,
        .ready = false,
        .continuation = *continuation,
        .operation_key = None(OperationKey),
        .operation_context = 0,
        .park_reason = park_reason,
        .blocked_operations = BlockedParkedOperations__new(),
        .pending_operations = 0,
        .return_code = None(ReturnCode),
    };
}

ParkedOperationPtr ParkedOperation__heap_new(KernelContextPtr kernel_context, OptionalPtr(Continuation) continuation,
                                             ParkReason park_reason)
{
    return ParkedOperation__heap_new_with_context_ownership(kernel_context, continuation, park_reason, false);
}

ParkedOperationPtr ParkedOperation__heap_new_with_context_ownership(KernelContextPtr kernel_context,
                                                                    OptionalPtr(Continuation) continuation,
                                                                    ParkReason park_reason, bool owns_kernel_context)
{
    ParkedOperationPtr context = ptr(Kernel__alloc_kernel(sizeof(ParkedOperation)), ParkedOperation);
    *context = ParkedOperation__new(kernel_context, continuation, park_reason, owns_kernel_context);

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

Result(CollectionCode)
    ParkedOperation__add_blocked_operation(ParkedOperationPtr self, ParkedOperationPtr blocked_operation)
{
    match_result(
        BlockedParkedOperations__push(&self->blocked_operations, blocked_operation),
        success(index)
        {
            self->pending_operations++;
            return Success(CollectionCode, index);
        },
        error(code, message)
        {
            return Error(CollectionCode, message, code);
        }
    );

    return Error(CollectionCode, bstring("Unable to add blocked operation"), LINKED_LIST_ALLOCATION_FAILED);
}

Result(CollectionCode)
    ParkedOperation__resolve_blocked_operation(ParkedOperationPtr self, ParkedOperationPtr blocked_operation)
{
    match_result(
        BlockedParkedOperations__remove(&self->blocked_operations, blocked_operation),
        success(index)
        {
            if (self->pending_operations > 0)
            {
                self->pending_operations--;
            }
            return Success(CollectionCode, index);
        },
        error(code, message)
        {
            return Error(CollectionCode, message, code);
        }
    );

    return Error(CollectionCode, bstring("Unable to resolve blocked operation"), LINKED_LIST_ITEM_NOT_FOUND);
}

void ParkedOperation__dispose(ParkedOperationPtr self)
{
    while (BlockedParkedOperations__len(&self->blocked_operations) > 0)
    {
        match_optional(
            BlockedParkedOperations__remove_at(&self->blocked_operations, 0),
            some(blocked_operation)
            {
                ParkedOperation__dispose(blocked_operation);
            },
            none_do_nothing()
        );
    }
    BlockedParkedOperations__dispose(&self->blocked_operations);
    if (self->owns_kernel_context)
    {
        KernelContext__dispose_heap(self->kernel_context);
    }
    Kernel__free(addr(self));
}

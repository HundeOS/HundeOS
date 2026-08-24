#include "kernel_context.h"
#include "../kernel_core.h"
#include "../kalloc.h"

KernelContext KernelContext__new(Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler)
{
    return (KernelContext) {
        .pid = pid,
        .scheduler = scheduler,
        .result_mapper = None(KernelResultMapper),
        .next_context = None(KernelContextPtr),
        .result_mapper_context = 0,
    };
}

KernelContextPtr KernelContext__heap_new(Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler)
{
    KernelContextPtr context = ptr(Kernel__alloc_kernel(sizeof(KernelContext)), KernelContext);

    *context = KernelContext__new(pid, scheduler);

    return context;
}

KernelContextPtr KernelContext__heap_new_with_mapper(
    Optional(ProcessID) pid, Optional(SchedulerPtr) scheduler,
    KernelResultMapper mapper, Anything mapper_context
)
{
    KernelContextPtr context = KernelContext__heap_new(pid, scheduler);
    KernelContext__set_result_mapper(context, mapper, mapper_context);
    return context;
}

void KernelContext__set_result_mapper(
    KernelContextPtr self, KernelResultMapper mapper, Anything mapper_context
)
{
    self->result_mapper = Some(KernelResultMapper, mapper);
    self->result_mapper_context = mapper_context;
}

Result(Anything) KernelContext__map_result(KernelContextPtr self, Result(Anything) result)
{
    match_optional(KernelResultMapper, self->result_mapper,
        some(mapper) { return mapper(result, self->result_mapper_context); },
        none_do_nothing()
    );

    return result;
}

bool KernelContext__has_any_state(KernelContextPtr self)
{
    bool has_state = false;

    match_optional(ProcessID, self->pid,
        some(pid) { has_state = true;; },
        none_do_nothing()
    );

    match_optional(SchedulerPtr, self->scheduler,
        some(scheduler) { has_state = true;; },
        none_do_nothing()
    );

    match_optional(KernelResultMapper, self->result_mapper,
        some(mapper) { has_state = true;; },
        none_do_nothing()
    );

    match_optional(KernelContextPtr, self->next_context,
        some(next_context) { has_state = true;; },
        none_do_nothing()
    );

    return has_state;
}

void KernelContext__dispose_heap(KernelContextPtr self)
{
    Kernel__free(addr(self));
}

KernelContextPtr KernelContext__from_originating_context_heap(OriginatingContext originating_context)
{
    Optional(ProcessID) pid = None(ProcessID);
    match_optional(ProcessPtr, originating_context.process,
        some(process) { pid = Some(ProcessID, process->pid); },
        none_do_nothing()
    );

    return KernelContext__heap_new(pid, Some(SchedulerPtr, originating_context.kernel->scheduler));
}

KernelContext KernelContext__from_originating_context(OriginatingContext originating_context)
{
    Optional(ProcessID) pid = None(ProcessID);
    match_optional(ProcessPtr, originating_context.process,
        some(process) { pid = Some(ProcessID, process->pid); },
        none_do_nothing()
    );

    return KernelContext__new(pid, Some(SchedulerPtr, originating_context.kernel->scheduler));
}

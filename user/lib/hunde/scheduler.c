#include "native.h"
#include "fiber/scheduler.h"
#include "sys/queue_wait.h"
#include <stdlib.h>

typedef struct RustScheduler
{
    FiberScheduler scheduler;
} RustScheduler;

typedef RustScheduler * RustSchedulerPtr;

RustNativeLayout hunde_rust_scheduler_layout(void)
{
    return RustNativeLayout__new(sizeof(RustScheduler), _Alignof(RustScheduler));
}

void hunde_rust_scheduler_init(void * storage)
{
    RustSchedulerPtr self = storage;
    self->scheduler = FiberScheduler__new();
}

static Result(FiberCode) RustScheduler__enter(FiberPtr fiber, Anything argument)
{
    ConstRustFiberTaskPtr task = (ConstRustFiberTaskPtr)argument;
    RustFiberEntry entry = task->entry;
    HundeHostContext context = task->context;
    HundeStatusResult result = entry(context);
    if (result.kind != HUNDE_ABI_SUCCESS)
    {
        return Error(FiberCode, bstr("Rust fibre failed"), result.error.code);
    }
    return Success(FiberCode, FIBER_OK);
}

HundeStatusResult hunde_rust_scheduler_spawn(void * storage, ConstRustFiberTaskPtr task)
{
    RustSchedulerPtr self = storage;
    match_result(
        FiberScheduler__spawn(&self->scheduler, RustScheduler__enter, (Anything)task),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

static Result(QueueWaitCode) RustScheduler__wait(Anything context)
{
    RustSchedulerPtr self = (RustSchedulerPtr)context;
    match_result(
        FiberScheduler__await_current(&self->scheduler, FiberWaitKey__new(FIBER_WAIT_USER, 1)),
        success(value)
        {
            return Success(QueueWaitCode, QUEUE_WAIT_CHANGED);
        },
        error(code, message)
        {
            return Error(QueueWaitCode, message, code);
        }
    );
    return Error(QueueWaitCode, bstr("Unknown error"), RESULT_UNKNOWN_ERROR_CODE);
}

static void RustScheduler__notify(Anything context)
{
    HundeStatusResult result = hunde_rust_scheduler_wake((void *)context);
    if (result.kind != HUNDE_ABI_SUCCESS)
    {
        abort();
    }
}

HundeStatusResult hunde_rust_scheduler_run(void * storage)
{
    RustSchedulerPtr self = storage;
    QueueWaitAdapter adapter = QueueWaitAdapter__new((Anything)self, RustScheduler__wait, RustScheduler__notify);
    Optional(QueueWaitAdapter) previous = QueueWait__swap_adapter(&Some(QueueWaitAdapter, adapter));
    Result(FiberCode) progress = FiberScheduler__run_ready_until_blocked(&self->scheduler);
    QueueWait__swap_adapter(&previous);
    match_result(
        progress,
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_rust_scheduler_park(void * storage)
{
    RustSchedulerPtr self = storage;
    match_result(
        FiberScheduler__await_current(&self->scheduler, FiberWaitKey__new(FIBER_WAIT_USER, 1)),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_rust_scheduler_wake(void * storage)
{
    RustSchedulerPtr self = storage;
    FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, 1);
    for (;;)
    {
        match_result(
            FiberScheduler__wake(&self->scheduler, key, FiberWakeValue__new(&key, FIBER_OK, 0)),
            success(value)
            {
                continue;
            },
            error(code, message)
            {
                if (code == FIBER_WAIT_NOT_FOUND)
                {
                    return HundeStatusResult__success();
                }
                return RustNative__error(code);
            }
        );
        return RustNative__unknown_error();
    }
}

uint64_t hunde_rust_scheduler_live(void * storage)
{
    RustSchedulerPtr self = storage;
    return FiberScheduler__live_count(&self->scheduler);
}

uint64_t hunde_rust_scheduler_waiting(void * storage)
{
    RustSchedulerPtr self = storage;
    return FiberScheduler__waiting_count(&self->scheduler);
}

#include "block_runtime.h"
#include "fiber/scheduler.h"
#include "sys/service.h"

typedef struct BlockRuntime
{
    BlockServerPtr server;
    FiberScheduler scheduler;
} BlockRuntime;
typedef BlockRuntime * BlockRuntimePtr;

static BlockRuntime BlockRuntime__new(BlockServerPtr server)
{
    return (BlockRuntime){.server = server, .scheduler = FiberScheduler__new()};
}

static Result(FiberCode) BlockRuntime__worker(FiberPtr fiber, Anything argument)
{
    BlockRuntimePtr self = (BlockRuntimePtr)argument;
    while (true)
    {
        if (!BlockServer__poll(self->server))
        {
            match_result(
                FiberScheduler__await_current(&self->scheduler, FiberWaitKey__new(FIBER_WAIT_USER, 0)),
                success_do_nothing(),
                error(code, message)
                {
                    return Error(FiberCode, message, code);
                }
            );
        }
    }
}

int BlockServer__run(BlockServerPtr self)
{
    BlockRuntime runtime = BlockRuntime__new(self);
    match_result(
        FiberScheduler__spawn(&runtime.scheduler, BlockRuntime__worker, (Anything)&runtime),
        success_do_nothing(),
        error(code, message)
        {
            return 1;
        }
    );
    while (true)
    {
        Channel__prepare_wait();
        ServiceEvent event;
        ServiceCode status = service_poll(&event);
        if (!service_alive(status))
        {
            return status == SERVICE_COMPLETE ? 0 : 1;
        }
        if (status != SERVICE_OK && status != SERVICE_EVENT_QUEUE_EMPTY)
        {
            return 1;
        }
        FiberWaitKey key = FiberWaitKey__new(FIBER_WAIT_USER, 0);
        match_result(
            FiberScheduler__wake(&runtime.scheduler, key, FiberWakeValue__new(&key, FIBER_OK, 0)),
            success_do_nothing(),
            error(code, message)
            {
                if (code != FIBER_WAIT_NOT_FOUND)
                {
                    return 1;
                }
            }
        );
        match_result(
            FiberScheduler__run_ready_until_blocked(&runtime.scheduler),
            success_do_nothing(),
            error(code, message)
            {
                return 1;
            }
        );
        if (status == SERVICE_EVENT_QUEUE_EMPTY)
        {
            Channel__wait_event();
        }
    }
}

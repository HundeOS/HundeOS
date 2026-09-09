#include "lifecycle.h"
#include "fiber/scheduler.h"
#include "sys/io.h"
#include "sys/print.h"
#include "sys/service.h"

#ifndef HUNDEKANAL_WARTER_ENTRYPOINT
#define HUNDEKANAL_WARTER_ENTRYPOINT main
#endif

typedef struct HundekanalWarterContext
{
    FiberScheduler scheduler;
    AtomicMessageQueueArrivalsPtr arrivals;
    ChannelExchanges exchanges;
    ChannelState wake_state;
    Channel wake;
    bool progress;
} HundekanalWarterContext;
typedef HundekanalWarterContext * HundekanalWarterContextPtr;

static HundekanalWarterContext HundekanalWarterContext__new(AtomicMessageQueueArrivalsPtr arrivals)
{
    return (HundekanalWarterContext){
        .scheduler = FiberScheduler__new(), .arrivals = arrivals, .exchanges = ChannelExchanges__new()};
}

static Result(FiberCode) HundekanalWarter__dispatch_fiber(FiberPtr self, Anything argument)
{
    HundekanalWarterContextPtr context = (HundekanalWarterContextPtr)argument;
    while (true)
    {
        context->progress = ChannelExchanges__step(&context->exchanges, context->arrivals);
        match_result(
            FiberScheduler__await_channel(&context->scheduler, &context->wake),
            success_do_nothing(),
            error(code, message) { return Error(FiberCode, message, code); }
        );
    }
}

static int HundekanalWarter__run(AtomicMessageQueueArrivalsPtr arrivals)
{
    HundekanalWarterContext context = HundekanalWarterContext__new(arrivals);
    context.wake = Channel__new(&context.wake_state);
    match_result(
        FiberScheduler__spawn(&context.scheduler, HundekanalWarter__dispatch_fiber, (Anything)&context),
        success_do_nothing(),
        error(code, message) { return 1; }
    );
    ChannelExchange__prepare_wait();
    while (true)
    {
        match_result(
            FiberScheduler__run_ready_until_blocked(&context.scheduler),
            success_do_nothing(),
            error(code, message) { return 1; }
        );
        if (!context.progress)
        {
            ChannelExchange__wait_event();
        }
        // An event is only a hint: the fibre scans every authoritative connection again.
        match_result(
            FiberScheduler__wake_channel(&context.scheduler, &context.wake),
            success_do_nothing(),
            error(code, message) { return 1; }
        );
    }
}

int HUNDEKANAL_WARTER_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    match_result(
        atomic_message_queue_listen(&bstr("hundekanal-warter")),
        success(arrivals)
        {
            if (!service_ok(service_publish_ready(&bstr("hundekanal-warter"))))
            {
                return 1;
            }
            IO__print(&bstr("hundekanal-warter: ready\n"));
            return HundekanalWarter__run(arrivals);
        },
        error(code, message) { IO__printf(&bstr("hundekanal-warter: listen failed %i\n"), (int)code); }
    );
    return 1;
}

#include "register.h"
#include "../subsystems/message/message_broker.h"
#include "../subsystems/message/subsystem_codes.h"
#include "../types/result.h"
#include "../debug.h"


void action_park_message(KernelListenerPtr self, MessagePtr message)
{
    Match(Payload, message->payload)
        IsSome(payload,
        {
            if (payload.type == PAYLOAD_TYPE_DEFERRED)
            {
                Deferred deferred = payload.deferred;
                __d_printf(SCHEDULER, "io parking process %p\n", self->kernel);
                Result(ProcessID) pid_res = Scheduler__park_by_pid(
                    self->kernel->scheduler, deferred.anything,
                    deferred.context, Some(Continuation, deferred.continuation),
                    PARK_WAIT_IO
                );

                ResultMatch(ProcessID, pid_res)
                    IsError(code, message, __d_printf(PANIC, "Failed to park process: %s", cstr(message)));
                End
            }
            else
            {
                __d_println(PANIC, "Invalid park payload");
            }

        })
        IsNone(__d_println(PANIC, "Invalid park message"));
    End

}

void action_unpark_message(KernelListenerPtr self, MessagePtr message)
{
    Match(Payload, message->payload)
        IsSome(payload,
        {
            if (payload.type == PAYLOAD_TYPE_ANYTHING)
            {
                ProcessID pid = payload.anything;
                Result(ProcessID) pid_res = Scheduler__unpark_by_pid(
                    self->kernel->scheduler, pid, PARK_WAIT_IO, None(ReturnCode)
                );

                ResultMatch(ProcessID, pid_res)
                    IsError(code, message, __d_printf(PANIC, "Failed to unpark process: %s", cstr(message)));
                End
            }
            else
            {
                __d_println(PANIC, "Invalid unpark payload");
            }

        })
        IsNone(__d_println(PANIC, "Invalid unpark message"));
    End

}

void Scheduler__io_event(KernelListenerPtr self, MessagePtr message)
{
    __d_printf(SCHEDULER, "io event: %d\n", message->source_code);
    if (message->source_code == SUBSYS_IO_PARK)
    {
        action_park_message(self, message);
    }
    else if (message->source_code == SUBSYS_IO_AVAILABLE)
    {
        action_unpark_message(self, message);
    }

}

Result(SchedulerRegisterCode) Scheduler__register(KernelPtr kernel)
{
    MessageBrokerPtr broker = kernel->message_broker;
    Listener listener = KernelListener__new(kernel, SOURCE_IO, Scheduler__io_event);

    ResultMatch(ListenerRegisterResult, MessageBroker__register_listener(broker, listener))
        IsSuccess(res, return Success(SchedulerRegisterCode, res));
        IsError(code, message, return Error(SchedulerRegisterCode, message, code));
    End
}
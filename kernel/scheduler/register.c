#include "register.h"
#include "../subsystems/message/io_operation_message.h"
#include "../subsystems/message/message_broker.h"
#include "../subsystems/message/subsystem_codes.h"
#include "../service/service.h"
#include "../types/result.h"
#include "../debug.h"


void action_park_message(KernelListenerPtr self, MessagePtr message)
{
    match_optional(Payload, message->payload,
        some(payload)
        {
            if (payload.type == PAYLOAD_TYPE_DEFERRED)
            {
                Deferred deferred = payload.deferred;
                __d_printf(SCHEDULER, "io parking process %p\n", self->kernel);
                Result(ProcessID) pid_res = Scheduler__park_by_pid_with_context_ownership_and_operation_key(
                    self->kernel->scheduler, deferred.anything,
                    deferred.context, &Some(Continuation, deferred.continuation),
                    PARK_WAIT_IO, deferred.owns_context, &deferred.operation_key
                );

                match_result(ProcessID, pid_res,
                    success_do_nothing(),
                    error(code, message) { __d_printf(PANIC, "Failed to park process: %s", cstr(message)); }
                );
            }
            else
            {
                __d_println(PANIC, "Invalid park payload");
            }

        },
        none() { __d_println(PANIC, "Invalid park message"); }
    );

}

void action_unpark_message(KernelListenerPtr self, MessagePtr message)
{
    match_optional(Payload, message->payload,
        some(payload)
        {
            if (payload.type == PAYLOAD_TYPE_PROCESS_ID)
            {
                ProcessID pid = payload.process_id;
                __d_printf(SCHEDULER, "scheduler unpark message pid=%i\n", pid);
                Result(ProcessID) pid_res = Scheduler__unpark_by_pid(
                    self->kernel->scheduler, pid, PARK_WAIT_IO, &None(ReturnCode)
                );

                match_result(ProcessID, pid_res,
                    success(unparked_pid)
                    {
                        __d_printf(SCHEDULER, "scheduler unpark message success pid=%i\n", unparked_pid);
                    },
                    error(code, message) { __d_printf(PANIC, "Failed to unpark process: %s", cstr(message)); }
                );
            }
            else
            {
                __d_println(PANIC, "Invalid unpark payload");
            }

        },
        none() { __d_println(PANIC, "Invalid unpark message"); }
    );

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
    else if (message->source_code == SUBSYS_IO_OPERATION_AVAILABLE)
    {
        match_optional(Payload, message->payload,
            some(payload)
            {
                if (payload.type == PAYLOAD_TYPE_IO_OPERATION)
                {
                    Result(ProcessID) pid_res = Scheduler__unpark_by_pid_with_operation_key(
                        self->kernel->scheduler,
                        payload.io_operation.pid,
                        PARK_WAIT_IO,
                        &None(ReturnCode),
                        &Some(OperationKey, payload.io_operation.operation_key)
                    );

                    match_result(ProcessID, pid_res,
                        success_do_nothing(),
                        error(code, error_message) { __d_printf(PANIC, "Failed to unpark process: %s", cstr(error_message)); }
                    );
                }
            },
            none_do_nothing()
        );
    }

}

void Scheduler__service_event(KernelListenerPtr self, MessagePtr message)
{
    if (message->source_code != SUBSYS_SERVICE_EVENT_AVAILABLE)
    {
        return;
    }

    match_optional(Payload, message->payload,
        some(payload)
        {
            if (payload.type == PAYLOAD_TYPE_SERVICE_EVENT_DELIVERY)
            {
                match_result(ServiceCode, Scheduler__service_enqueue(
                    self->kernel->scheduler,
                    payload.service_event_delivery.process,
                    &payload.service_event_delivery.event
                ),
                    success_do_nothing(),
                    error(code, message) { __d_printf(PANIC, "Failed to enqueue service event: %s", cstr(message)); }
                );
            }
            else
            {
                __d_println(PANIC, "Invalid service event payload");
            }
        },
        none() { __d_println(PANIC, "Invalid service event message"); }
    );
}

Result(SchedulerRegisterCode) Scheduler__register(KernelPtr kernel)
{
    MessageBrokerPtr broker = kernel->message_broker;
    Listener io_listener = KernelListener__new(kernel, SOURCE_IO, Scheduler__io_event);
    Listener service_listener = KernelListener__new(kernel, SOURCE_SERVICE, Scheduler__service_event);

    match_result(ListenerRegisterResult, MessageBroker__register_listener(broker, &io_listener),
        success_do_nothing(),
        error(code, message) { return Error(SchedulerRegisterCode, message, code); }
    );

    match_result(ListenerRegisterResult, MessageBroker__register_listener(broker, &service_listener),
        success(res)
        {
            return Success(SchedulerRegisterCode, res);
        },
        error(code, message) { return Error(SchedulerRegisterCode, message, code); }
    );

    return Success(SchedulerRegisterCode, LISTENER_REGISTER_OK);
}

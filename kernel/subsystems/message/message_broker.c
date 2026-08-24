#include "message_broker.h"
#include "subsystem_codes.h"
#include "../../kalloc.h"

FixedArrayBodyDefine(Listeners);

static BString too_many_listeners = BSTRING_NEW("too many listeners already registered");


MessageBroker MessageBroker__new()
{
    return (MessageBroker) {
        .listeners = EmptyArray(Listeners),
        .lock = MUTEX_INIT
    };
}

MessageBrokerPtr MessageBroker__heap_new()
{
    MessageBrokerPtr broker = ptr(Kernel__alloc_kernel(sizeof(MessageBroker)), MessageBroker);
    *broker = MessageBroker__new();
    return broker;
}

Message Message__new_anything(Source source, SourceCode source_code, Anything anything)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_ANYTHING,
        .anything = anything
    };

    return (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };

}

Message Message__new_process_id(Source source, SourceCode source_code, ProcessID process_id)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_PROCESS_ID,
        .process_id = process_id
    };

    return (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
}

Message Message__new_io_operation(
    Source source, SourceCode source_code, ProcessID pid, OperationKey operation_key
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_IO_OPERATION,
        .io_operation = IOOperationMessage__new(pid, operation_key)
    };

    return (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
}

Message Message__new_service_event_delivery(
    Source source, SourceCode source_code, ProcessPtr process, ServiceEvent event
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_SERVICE_EVENT_DELIVERY,
        .service_event_delivery = (ServiceEventDelivery) {
            .process = process,
            .event = event,
        }
    };

    return (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
}

Message Message__new_deferred(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation, bool owns_context,
    Optional(OperationKey) operation_key
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_DEFERRED,
        .deferred = (Deferred) {
                        .anything = anything,
                        .continuation = continuation,
                        .context = context,
                        .owns_context = owns_context,
                        .operation_key = operation_key,
                    }
    };

    return (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
}


MessagePtr Message__heap_new_anything(Source source, SourceCode source_code, Anything anything)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_anything(source, source_code, anything);

    return message;
}

MessagePtr Message__heap_new_process_id(Source source, SourceCode source_code, ProcessID process_id)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_process_id(source, source_code, process_id);
    return message;
}

MessagePtr Message__heap_new_io_operation(
    Source source, SourceCode source_code, ProcessID pid, OperationKey operation_key
)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_io_operation(source, source_code, pid, operation_key);
    return message;
}

MessagePtr Message__heap_new_service_event_delivery(
    Source source, SourceCode source_code, ProcessPtr process, ServiceEvent event
)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_service_event_delivery(source, source_code, process, event);
    return message;
}

MessagePtr Message__heap_new_deferred(
    Source source, SourceCode source_code, Anything anything, KernelContextPtr context, Continuation continuation
)
{
    return Message__heap_new_deferred_with_context_ownership(
        source, source_code, anything, context, continuation, false
    );
}

MessagePtr Message__heap_new_deferred_with_context_ownership(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation, bool owns_context
)
{
    return Message__heap_new_deferred_with_context_ownership_and_operation_key(
        source, source_code, anything, context, continuation, owns_context, None(OperationKey)
    );
}

MessagePtr Message__heap_new_deferred_with_context_ownership_and_operation_key(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation,
    bool owns_context, Optional(OperationKey) operation_key
)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_deferred(
        source, source_code, anything, context, continuation, owns_context, operation_key
    );

    return message;
}

void Message__dispose(MessagePtr self)
{
    if (self == NULL) // hundelint: ignore-no-null
    {
        return;
    }

    // Payload storage is inline. Deferred context ownership is transferred to
    // parked-operation lifecycle management rather than being owned by Message.
    Kernel__free(addr(self));
}


Result(AckCode) MessageBroker__deliver(MessageBrokerPtr self, MessagePtr message)
{
    __d_printf(BROKER, "delivering message %i %i\n", message->source, message->source_code);
    Listeners clone = Listeners__new();

    critical_section(self->lock,
    {
        Listeners__push_all(&clone, &self->listeners);
    });

    for_each_fa(Listeners, clone, index, listener,
    {
        if (listener.source == message->source)
        {
            listener.dispatch(&listener, message);
        }
    });
    Message__dispose(message);
    return Success(AckCode, ACK_OK);
}

Result(ListenerRegisterResult) MessageBroker__register_listener(MessageBrokerPtr self, Listener listener)
{
    critical_section(self->lock,
    {
        match_result(CollectionCode, Listeners__push(&self->listeners, listener),
            success_do_nothing(),
            error(code, message)
            {
                cs_return Error(ListenerRegisterResult, too_many_listeners, LISTENER_REGISTER_ERROR);
            }
        );
    });
    return Success(ListenerRegisterResult, LISTENER_REGISTER_OK);
}

void Listener__kernel_dispatch(ListenerPtr self, MessagePtr message)
{
    if (self->type == LISTENER_TYPE_KERNEL)
    {
        self->kernel_listener.callback(&self->kernel_listener, message);
    }
}

Listener KernelListener__new(KernelPtr kernel, Source source, KernelCallback callback)
{
    return (Listener) {
        .type = LISTENER_TYPE_KERNEL,
        .dispatch = Listener__kernel_dispatch,
        .source = source,
        .kernel_listener = (KernelListener) {
            .kernel = kernel,
            .callback = callback
        }
    };
}

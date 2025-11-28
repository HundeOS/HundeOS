#include "message_broker.h"

FixedArrayBodyDefine(Listeners);

static KString too_many_listeners = KSTRING_NEW("too many listeners already registered");


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

MessagePtr Message__heap_new_anything(Source source, SourceCode source_code, Anything anything)
{
    MessagePtr message = ptr(Kernel__alloc_kernel(sizeof(Message)), Message);
    *message = Message__new_anything(source, source_code, anything);

    return message;
}

Result(AckCode) MessageBroker__deliver(MessageBrokerPtr self, MessagePtr message)
{
    CriticalSection(self->lock,
    {
        FAIterate(Listeners, self->listeners, index, listener)
            if (listener.source == message->source)
            {
                listener.dispatch(&listener, message);
            }
        End
    })
    return Success(AckCode, ACK_OK);
}

Result(ListenerRegisterResult) MessageBroker__register_listener(MessageBrokerPtr self, Listener listener)
{
    CriticalSection(self->lock,
    {
        ResultMatch(ArrayCode, Listeners__push(&self->listeners, listener))
            IsError(code, message,
            {
                _cs_return Error(ListenerRegisterResult, too_many_listeners, LISTENER_REGISTER_ERROR);
            })
        End
    })
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


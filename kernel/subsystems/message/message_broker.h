#ifndef __KERNEL_SUBSYSTEMS_MESSAGE_BROKER_H
#define __KERNEL_SUBSYSTEMS_MESSAGE_BROKER_H
#include <stdbool.h>
#include <stdint.h>
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../types/result.h"
#include "../../all_defs.h"
#include "../../mutex.h"
#include "../../process.h"
#include "../../scheduler/types.h"
#include "../../service/service.h"
#include "io_operation_message.h"


typedef struct Kernel Kernel;
typedef Kernel * KernelPtr;

typedef uint64_t Source;
typedef uint64_t SourceCode;
typedef uint64_t Destination;
typedef uint64_t PayloadType;
typedef uint64_t ListenerType;
typedef uint64_t ListenerRegisterResult;

typedef uint8_t PayloadData;
typedef PayloadData * PayloadDataPtr;

typedef uint64_t AckCode;

ResultDef(AckCode);
ResultDef(ListenerRegisterResult);

typedef enum ListenerTypes
{
    LISTENER_TYPE_KERNEL = 0,
    LISTENER_TYPE_PROCESS = 1,
}
ListenerTypes;

typedef struct Message Message;
typedef Message * MessagePtr;

typedef struct KernelListener KernelListener;
typedef KernelListener * KernelListenerPtr;
typedef void (*KernelCallback)(KernelListenerPtr self, MessagePtr message);

typedef struct KernelListener
{
    KernelPtr kernel;
    KernelCallback callback;
}
KernelListener;

typedef struct ProcessListener
{
    ProcessID process_id;
}
ProcessListener;

typedef struct Listener Listener;
typedef Listener * ListenerPtr;

typedef void (*ListenerDispatch)(ListenerPtr self, MessagePtr message);

typedef struct Listener
{
    ListenerType type;
    ListenerDispatch dispatch;
    Source source;

    union
    {
        KernelListener kernel_listener;
        ProcessListener process_listener;
    };
}
Listener;

typedef Listener * ListenerPtr;
OptionalDef(ListenerPtr);
OptionalDef(Listener);

FixedArrayDefine(Listener, MAX_LISTENERS, Listeners, Ptr__equals);

typedef struct KernelContext KernelContext;
typedef KernelContext * KernelContextPtr;

typedef struct Deferred
{
    Anything anything;
    Continuation continuation;
    KernelContextPtr context;
    bool owns_context;
    Optional(OperationKey) operation_key;
} Deferred;


typedef struct Payload
{
    PayloadType type;
    union
    {
        Anything anything;
        ProcessID process_id;
        Deferred deferred;
        IOOperationMessage io_operation;
        ServiceEventDelivery service_event_delivery;
        PayloadData data[MAX_PAYLOAD_SIZE];
    };
}
Payload;

OptionalDef(Payload);

typedef struct Message
{
    Source source;
    SourceCode source_code;

    Optional(Payload) payload;
}
Message;

OptionalDef(Message);

typedef Message * MessagePtr;

typedef struct MessageBroker
{
    Listeners listeners;
    mutex_t lock;
}
MessageBroker;

typedef MessageBroker * MessageBrokerPtr;

Listener KernelListener__new(KernelPtr kernel, Source source, KernelCallback callback);

MessageBroker MessageBroker__new();
MessageBrokerPtr MessageBroker__heap_new();

Result(ListenerRegisterResult) MessageBroker__register_listener(MessageBrokerPtr self, ListenerPtr listener);
Result(AckCode) MessageBroker__deliver(MessageBrokerPtr self, MessagePtr message);
void Message__dispose(MessagePtr self);

Message Message__new_anything(Source source, SourceCode source_code, Anything anything);
MessagePtr Message__heap_new_anything(Source source, SourceCode source_code, Anything anything);
Message Message__new_process_id(Source source, SourceCode source_code, ProcessID process_id);
MessagePtr Message__heap_new_process_id(Source source, SourceCode source_code, ProcessID process_id);
Message Message__new_io_operation(
    Source source, SourceCode source_code, ProcessID pid, OperationKey operation_key
);
MessagePtr Message__heap_new_io_operation(
    Source source, SourceCode source_code, ProcessID pid, OperationKey operation_key
);
Message Message__new_service_event_delivery(
    Source source, SourceCode source_code, ProcessPtr process, ServiceEvent event
);
MessagePtr Message__heap_new_service_event_delivery(
    Source source, SourceCode source_code, ProcessPtr process, ServiceEvent event
);
MessagePtr Message__heap_new_deferred(
    Source source, SourceCode source_code, Anything anything, KernelContextPtr context, Continuation continuation
);
MessagePtr Message__heap_new_deferred_with_context_ownership(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation, bool owns_context
);
MessagePtr Message__heap_new_deferred_with_context_ownership_and_operation_key(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation,
    bool owns_context, OptionalPtr(OperationKey) operation_key
);

#define NEW_BROKER \
    (MessageBroker) { \
        .listeners = EmptyArray(Listeners), \
    }


#endif

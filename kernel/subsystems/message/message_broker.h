#ifndef __KERNEL_SUBSYSTEMS_MESSAGE_BROKER_H
#define __KERNEL_SUBSYSTEMS_MESSAGE_BROKER_H
#include <stdint.h>
#include "../../types/fixed_array.h"
#include "../../types/optional.h"
#include "../../types/result.h"
#include "../../all_defs.h"
#include "../../process.h"
#include "../../scheduler/types.h"


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
} Deferred;


typedef struct Payload
{
    PayloadType type;
    union
    {
        Anything anything;
        Deferred deferred;
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

Result(ListenerRegisterResult) MessageBroker__register_listener(MessageBrokerPtr self, Listener listener);
Result(AckCode) MessageBroker__deliver(MessageBrokerPtr self, MessagePtr message);

Message Message__new_anything(Source source, SourceCode source_code, Anything anything);
MessagePtr Message__heap_new_anything(Source source, SourceCode source_code, Anything anything);
MessagePtr Message__heap_new_deferred(
    Source source, SourceCode source_code, Anything anything, KernelContextPtr context, Continuation continuation
);

#define NEW_BROKER \
    (MessageBroker) { \
        .listeners = EmptyArray(Listeners), \
    }


#endif
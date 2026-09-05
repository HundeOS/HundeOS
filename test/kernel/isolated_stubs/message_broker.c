#include "../../../kernel/subsystems/message/message_broker.h"
#include "../../../kernel/subsystems/message/subsystem_codes.h"

static Message stub_message;

MessagePtr Message__heap_new_anything(Source source, SourceCode source_code, Anything anything)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_ANYTHING,
        .anything = anything,
    };
    stub_message = (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
    return &stub_message;
}

MessagePtr Message__heap_new_process_id(Source source, SourceCode source_code, ProcessID process_id)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_PROCESS_ID,
        .process_id = process_id,
    };
    stub_message = (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
    return &stub_message;
}

MessagePtr Message__heap_new_io_operation(
    Source source, SourceCode source_code, ProcessID pid, OperationKey operation_key
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_IO_OPERATION,
        .io_operation = IOOperationMessage__new(pid, operation_key),
    };
    stub_message = (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
    return &stub_message;
}

MessagePtr Message__heap_new_service_event_delivery(
    Source source, SourceCode source_code, ProcessPtr process, ServiceEvent event
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_SERVICE_EVENT_DELIVERY,
        .service_event_delivery = (ServiceEventDelivery) {
            .process = process,
            .event = event,
        },
    };
    stub_message = (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
    return &stub_message;
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
        source, source_code, anything, context, continuation, owns_context, &None(OperationKey)
    );
}

MessagePtr Message__heap_new_deferred_with_context_ownership_and_operation_key(
    Source source, SourceCode source_code, Anything anything,
    KernelContextPtr context, Continuation continuation,
    bool owns_context, OptionalPtr(OperationKey) operation_key
)
{
    Payload payload = (Payload) {
        .type = PAYLOAD_TYPE_DEFERRED,
        .deferred = (Deferred) {
            .anything = anything,
            .continuation = continuation,
            .context = context,
            .owns_context = owns_context,
            .operation_key = *operation_key,
        }
    };
    stub_message = (Message) {
        .source = source,
        .source_code = source_code,
        .payload = Some(Payload, payload),
    };
    return &stub_message;
}

Result(AckCode) MessageBroker__deliver(MessageBrokerPtr self, MessagePtr message)
{
    return Success(AckCode, ACK_OK);
}

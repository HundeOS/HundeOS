#include "client.h"

#include <stdarg.h>

#include "channel/channel.h"
#include "services/service_directory.h"

static Channel trace_channel;
static bool trace_connected;

static TraceStatus DriverTrace__submit(ConstTraceRequestPtr request)
{
    if (!trace_connected)
    {
        match_result(
            Channel__open(ServiceDirectory__shared(), &bstr(HUND_VERFOLGEN_SERVICE)),
            success(channel)
            {
                trace_channel = channel;
                trace_connected = true;
            },
            error(code, message)
            {
                return TRACE_UNAVAILABLE;
            }
        );
    }

    TraceReply reply;
    match_result(
        Channel__submit(&trace_channel, (ConstChannelDataPtr)request, sizeof(*request), (ChannelDataPtr)&reply,
                        sizeof(reply)),
        success(size)
        {
            if (size == sizeof(reply))
            {
                return reply.status;
            }
        },
        error(code, message)
        {
            Channel__close(&trace_channel);
            trace_connected = false;
            return TRACE_UNAVAILABLE;
        }
    );

    Channel__close(&trace_channel);
    trace_connected = false;
    return TRACE_INVALID_REQUEST;
}

TraceStatus DriverTrace__write(ConstBStringPtr tag, ConstBStringPtr message)
{
    TraceRequest request = TraceRequest__record(tag, message);
    return DriverTrace__submit(&request);
}

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    BString format_string = BString__new(format);
    va_list args;
    va_start(args, format);
    BString message = BString__vsprintf(&format_string, args);
    va_end(args);
    BString tag_string = BString__new(tag);
    return DriverTrace__write(&tag_string, &message);
}

TraceStatus DriverTrace__storage_ready(void)
{
    TraceRequest request = TraceRequest__storage_ready();
    return DriverTrace__submit(&request);
}

#include "channel/exchange.h"
#include "sys/io.h"
#include "sys/print.h"

#ifndef CHANNEL_ECHO_CLIENT_ENTRYPOINT
#define CHANNEL_ECHO_CLIENT_ENTRYPOINT main
#endif

static int ChannelEchoClient__exchange(AtomicMessageQueuePtr warden, uint64_t target, ConstBStringPtr text)
{
    uint64_t length = BString__length(text);
    if (length >= CHANNEL_EXCHANGE_CAPACITY)
    {
        return 1;
    }
    ServiceQueueMessage request = ChannelExchange__message(CHANNEL_REQUEST_OPEN, target, 0, warden->connection);
    atomic_message_queue_wait_writable(warden);
    match_result(AtomicMessageQueue__push(warden, &request),
        success_do_nothing(),
        error(code, message) { return 1; }
    );
    atomic_message_queue_wait_readable(warden);
    uint64_t address = 0;
    match_result(
        AtomicMessageQueue__pop(warden),
        success(reply)
        {
            if (reply.kind != CHANNEL_RESPONSE_OPEN || reply.arg2 != warden->connection || reply.arg0 == 0)
            {
                return 1;
            }
            address = reply.arg0;
        },
        error(code, message) { return 1; }
    );
    ChannelPayloadPtr payload = (ChannelPayloadPtr)address;
    for (uint64_t index = 0; index < length; index++)
    {
        payload->request[index] = BString__char_at_or(text, (StringPos)index, '\0');
    }
    request = ChannelExchange__message(CHANNEL_REQUEST_SUBMIT, 0, length, warden->connection);
    atomic_message_queue_wait_writable(warden);
    match_result(AtomicMessageQueue__push(warden, &request),
        success_do_nothing(),
        error(code, message) { return 1; }
    );
    atomic_message_queue_wait_readable(warden);
    match_result(
        AtomicMessageQueue__pop(warden),
        success(reply)
        {
            if (reply.kind != CHANNEL_RESPONSE_REPLIED || reply.arg2 != warden->connection || reply.arg1 != length)
            {
                return 1;
            }
            BString result = BString__from_chars((const char *)payload->reply, (StringPos)length);
            IO__print(&result);
            IO__print(&bstr("\n"));
        },
        error(code, message) { return 1; }
    );
    return 0;
}

static int ChannelEchoClient__run_connected(ServiceDirectoryPtr directory, ConstBStringPtr text, uint64_t target)
{
    match_optional(
        ServiceDirectory__find_endpoint(directory, &bstr("hundekanal-warter")),
        some(warden)
        {
            match_result(
                atomic_message_queue_connect(warden),
                success(queue)
                {
                    int result = ChannelEchoClient__exchange(&queue, target, text);
                    // Closing is the final acknowledgement: no payload access may follow it.
                    if (atomic_message_queue_close(queue.connection) != ATOMIC_MESSAGE_QUEUE_OK)
                    {
                        result = 1;
                    }
                    if (result != 0)
                    {
                        IO__print(&bstr("channel_echo_client: channel failed or peer disconnected\n"));
                    }
                    return result;
                },
                error(code, message)
                {
                    IO__printf(&bstr("channel_echo_client: connect failed %i\n"), (int)code);
                    return 1;
                }
            );
        },
        none()
        {
            IO__print(&bstr("channel_echo_client: service unavailable\n"));
            return 1;
        }
    );
    return 1;
}

static int ChannelEchoClient__run(ServiceDirectoryPtr directory, ConstBStringPtr text)
{
    match_optional(
        ServiceDirectory__find_endpoint(directory, &bstr("hundekanal-test-echo")),
        some(target) { return ChannelEchoClient__run_connected(directory, text, target); },
        none()
        {
            IO__print(&bstr("channel_echo_client: service unavailable\n"));
            return 1;
        }
    );
    return 1;
}

int CHANNEL_ECHO_CLIENT_ENTRYPOINT(int argc, char ** argv)
{
    BString text = argc > 1 ? BString__new(argv[1]) : bstr("hello from channel_echo_client");
    return ChannelEchoClient__run(ServiceDirectory__shared(), &text);
}

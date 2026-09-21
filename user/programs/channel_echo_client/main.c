#include "channel/channel.h"
#include "sys/io.h"
#include "sys/print.h"

#ifndef CHANNEL_ECHO_CLIENT_ENTRYPOINT
#define CHANNEL_ECHO_CLIENT_ENTRYPOINT main
#endif

static int ChannelEchoClient__exchange(ChannelPtr client, ConstBStringPtr text)
{
    uint64_t length = BString__length(text);
    uint8_t request[CHANNEL_PAYLOAD_CAPACITY];
    uint8_t reply[CHANNEL_PAYLOAD_CAPACITY];
    for (uint64_t index = 0; index < length; index++)
    {
        request[index] = (uint8_t)BString__char_at_or(text, (StringPos)index, '\0');
    }
    match_result(
        Channel__submit(client, request, length, reply, sizeof(reply)),
        success(reply_size)
        {
            if (reply_size != length)
            {
                return 1;
            }
            BString result = BString__from_chars((const char *)reply, (StringPos)reply_size);
            IO__print(&result);
            IO__print(&bstr("\n"));
        },
        error(code, message)
        {
            return 1;
        }
    );
    return 0;
}

static int ChannelEchoClient__run(ServiceDirectoryPtr directory, ConstBStringPtr text)
{
    match_result(
        Channel__open(directory, &bstr("hundekanal-test-echo")),
        success(client)
        {
            int result = ChannelEchoClient__exchange(&client, text);
            if (Channel__close(&client) != CHANNEL_OK)
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
            IO__print(&bstr("channel_echo_client: service unavailable\n"));
            return 1;
        }
    );
}

int CHANNEL_ECHO_CLIENT_ENTRYPOINT(int argc, char ** argv)
{
    BString text = argc > 1 ? BString__new(argv[1]) : bstr("hello from channel_echo_client");
    return ChannelEchoClient__run(ServiceDirectory__shared(), &text);
}

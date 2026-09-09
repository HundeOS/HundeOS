#include "sys/io_server.h"
#include "sys/service.h"
#include "../../user.h"

static const char * HELLO_PATH = "/dev/hello";
static const BString hello_message = bstr("hello world\n");

static uint64_t copy_hello(IOServerBufferDataPtr target, uint64_t capacity)
{
    uint64_t bytes = (uint64_t)BString__length(&hello_message);
    if (bytes > capacity)
    {
        bytes = capacity;
    }

    for (uint64_t i = 0; i < bytes; i++)
    {
        target[i] = BString__char_at_or(&hello_message, (StringPos)i, '\0');
    }

    return bytes;
}

static void publish_hello(IOHandle handle)
{
    IOServerBuffer buffer;
    IOStatus status = io_server_get_buffer(handle, &buffer);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hello_stream: get buffer error %i\n"), (int)status);
        return;
    }

    uint64_t byte_count = copy_hello(buffer.data, buffer.capacity);
    status = io_server_data_available(handle, byte_count);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hello_stream: data available error %i\n"), (int)status);
        return;
    }

    status = io_server_eof(handle);
    if (status != IO_OK)
    {
        IO__printf(&bstr("hello_stream: eof error %i\n"), (int)status);
    }
}

int main(void)
{
    BString hello_path = bstring(HELLO_PATH);
    IOHandle handle = io_server_register(&hello_path, IO_SERVER_READ);
    if (handle < 0)
    {
        IO__printf(&bstr("hello_stream: register error %i\n"), (int)handle);
        return 1;
    }

    while (true)
    {
        ServiceEvent event;
        ServiceCode result = service_wait(&event);
        if (!service_ok(result))
        {
            continue;
        }

        if (event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_READ_REQUEST)
        {
            publish_hello(handle);
        }
    }
}

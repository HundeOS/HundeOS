#include "../../user.h"
#include "sys/io.h"

static BString default_socket_uri = bstr("socket://test/socket_test");
static BString default_message = bstr("ping");
static BString reply_message = bstr("pong");

static void SocketTest__usage(void)
{
    IO__print(&bstr("socket_test: usage socket_test listen [socket://path]\n"));
    IO__print(&bstr("socket_test: usage socket_test connect [socket://path] [message]\n"));
}

static BString SocketTest__uri_from_arg(int argc, char ** argv)
{
    if (argc > 2)
    {
        return BString__new(argv[2]);
    }

    return default_socket_uri;
}

static int SocketTest__listen(int argc, char ** argv)
{
    BString uri = SocketTest__uri_from_arg(argc, argv);
    IOHandle listener = IO__open(&uri, IO_OPEN_BIND | IO_OPEN_LISTEN);
    if (listener < 0)
    {
        IO__printf(&bstr("socket_test: listen error %i\n"), (int)listener);
        return 1;
    }

    IO__printf(&bstr("socket_test: listening %B\n"), &uri);
    IOHandle peer = IO__accept(listener);
    if (peer < 0)
    {
        IO__printf(&bstr("socket_test: accept error %i\n"), (int)peer);
        close(listener);
        return 1;
    }

    BString received = empty_bstr();
    ByteCount bytes = IO__read(peer, &received);
    if (bytes < 0)
    {
        IO__printf(&bstr("socket_test: read error %i\n"), (int)bytes);
        close(peer);
        close(listener);
        return 1;
    }

    IO__printf(&bstr("socket_test: received %B\n"), &received);
    bytes = IO__write(peer, &reply_message);
    if (bytes < 0)
    {
        IO__printf(&bstr("socket_test: write error %i\n"), (int)bytes);
        close(peer);
        close(listener);
        return 1;
    }

    close(peer);
    close(listener);
    return 0;
}

static BString SocketTest__message_from_arg(int argc, char ** argv)
{
    if (argc > 3)
    {
        return BString__new(argv[3]);
    }

    return default_message;
}

static int SocketTest__connect(int argc, char ** argv)
{
    BString uri = SocketTest__uri_from_arg(argc, argv);
    BString message = SocketTest__message_from_arg(argc, argv);
    IOHandle socket = IO__open(&uri, IO_OPEN_READ | IO_OPEN_WRITE);
    if (socket < 0)
    {
        IO__printf(&bstr("socket_test: connect error %i\n"), (int)socket);
        return 1;
    }

    ByteCount bytes = IO__write(socket, &message);
    if (bytes < 0)
    {
        IO__printf(&bstr("socket_test: write error %i\n"), (int)bytes);
        close(socket);
        return 1;
    }

    BString received = empty_bstr();
    bytes = IO__read(socket, &received);
    if (bytes < 0)
    {
        IO__printf(&bstr("socket_test: read error %i\n"), (int)bytes);
        close(socket);
        return 1;
    }

    IO__printf(&bstr("socket_test: reply %B\n"), &received);
    close(socket);
    return 0;
}

int main(int argc, char ** argv)
{
    if (argc < 2)
    {
        SocketTest__usage();
        return 1;
    }

    BString mode = BString__new(argv[1]);
    if (BString__equals(&mode, &bstr("listen")))
    {
        return SocketTest__listen(argc, argv);
    }

    if (BString__equals(&mode, &bstr("connect")))
    {
        return SocketTest__connect(argc, argv);
    }

    SocketTest__usage();
    return 1;
}

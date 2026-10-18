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

static int SocketTest__serve(IOHandle listener)
{
    match_result(
        IO__accept(listener),
        success(peer)
        {
            BString received = empty_bstr();
            match_result(
                IO__read(peer, &received),
                success(bytes)
                {
                    IO__printf(&bstr("socket_test: received %B\n"), &received);
                    match_result(
                        IO__write(peer, &reply_message),
                        success(written)
                        {
                            IO__close(peer);
                            IO__close(listener);
                            return 0;
                        },
                        error(code, message)
                        {
                            IO__printf(&bstr("socket_test: write error %i\n"), code);
                            IO__close(peer);
                            IO__close(listener);
                            return 1;
                        }
                    );
                },
                error(code, message)
                {
                    IO__printf(&bstr("socket_test: read error %i\n"), code);
                    IO__close(peer);
                    IO__close(listener);
                    return 1;
                }
            );
        },
        error(code, message)
        {
            IO__printf(&bstr("socket_test: accept error %i\n"), code);
            IO__close(listener);
            return 1;
        }
    );
    return 1;
}

static int SocketTest__listen(int argc, char ** argv)
{
    BString uri = SocketTest__uri_from_arg(argc, argv);
    match_result(
        IO__open(&uri, IO_OPEN_BIND | IO_OPEN_LISTEN),
        success(listener)
        {
            IO__printf(&bstr("socket_test: listening %B\n"), &uri);
            return SocketTest__serve(listener);
        },
        error(code, message)
        {
            IO__printf(&bstr("socket_test: listen error %i\n"), code);
            return 1;
        }
    );
    return 1;
}

static BString SocketTest__message_from_arg(int argc, char ** argv)
{
    if (argc > 3)
    {
        return BString__new(argv[3]);
    }

    return default_message;
}

static int SocketTest__exchange(IOHandle socket, ConstBStringPtr message)
{
    match_result(
        IO__write(socket, message),
        success(written)
        {
            BString received = empty_bstr();
            match_result(
                IO__read(socket, &received),
                success(bytes)
                {
                    IO__printf(&bstr("socket_test: reply %B\n"), &received);
                    IO__close(socket);
                    return 0;
                },
                error(code, message)
                {
                    IO__printf(&bstr("socket_test: read error %i\n"), code);
                    IO__close(socket);
                    return 1;
                }
            );
        },
        error(code, message)
        {
            IO__printf(&bstr("socket_test: write error %i\n"), code);
            IO__close(socket);
            return 1;
        }
    );
    return 1;
}

static int SocketTest__connect(int argc, char ** argv)
{
    BString uri = SocketTest__uri_from_arg(argc, argv);
    BString message = SocketTest__message_from_arg(argc, argv);
    match_result(
        IO__open(&uri, IO_OPEN_READ | IO_OPEN_WRITE),
        success(socket)
        {
            return SocketTest__exchange(socket, &message);
        },
        error(code, error_message)
        {
            IO__printf(&bstr("socket_test: connect error %i\n"), code);
            return 1;
        }
    );
    return 1;
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

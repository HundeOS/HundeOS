#include "../../user.h"
#include "sys/io.h"

static BString default_socket_uri = bstr("socket://test/socket_test");
static BString default_message = bstr("ping");
static BString reply_message = bstr("pong");

static void SocketTest__usage(void)
{
    IO__print(&bstr("socket_test: usage socket_test listen [socket://path]\n"));
    IO__print(&bstr("socket_test: usage socket_test connect [socket://path] [message]\n"));
    IO__print(&bstr("socket_test: usage socket_test bulk-listen|bulk-connect tcp://localhost:port\n"));
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

static int SocketTest__bulk_transfer(IOHandle socket, bool writing)
{
    ByteCount remaining = 32768;
    while (remaining > 0)
    {
        BString expected = empty_bstr();
        BString received = empty_bstr();
        ByteCount count = remaining < BSTRING_MAX_SIZE - 1 ? remaining : BSTRING_MAX_SIZE - 1;
        for (ByteCount index = 0; index < count; index++)
        {
            BString__push_char(&expected, (char)((32768 - remaining + index) % 251));
        }
        Result(ByteCount) transfer = writing ? IO__write(socket, &expected) : IO__read(socket, &received);
        match_result(
            transfer,
            success(bytes)
            {
                BString prefix = BString__slice(&expected, 0, bytes);
                if (bytes == 0 || bytes > count || (!writing && !BString__equals(&prefix, &received)))
                {
                    IO__print(&bstr("socket_test: bulk data mismatch or premature EOF\n"));
                    return 1;
                }
                remaining -= bytes;
            },
            error(code, message)
            {
                IO__printf(&bstr("socket_test: bulk transfer error %i\n"), code);
                return 1;
            }
        );
    }
    if (writing)
    {
        IO__print(&bstr("socket_test: bulk sent 32768\n"));
    }
    else
    {
        IO__print(&bstr("socket_test: bulk received 32768\n"));
    }
    return 0;
}

static int SocketTest__bulk(int argc, char ** argv, bool listening)
{
    BString uri = SocketTest__uri_from_arg(argc, argv);
    Flags flags = listening ? IO_OPEN_BIND | IO_OPEN_LISTEN : IO_OPEN_READ | IO_OPEN_WRITE;
    match_result(
        IO__open(&uri, flags),
        success(socket)
        {
            if (!listening)
            {
                int status = SocketTest__bulk_transfer(socket, true);
                IO__close(socket);
                return status;
            }
            IO__printf(&bstr("socket_test: bulk listening %B\n"), &uri);
            match_result(
                IO__accept(socket),
                success(peer)
                {
                    int status = SocketTest__bulk_transfer(peer, false);
                    IO__close(peer);
                    IO__close(socket);
                    return status;
                },
                error(code, message)
                {
                    IO__printf(&bstr("socket_test: bulk accept error %i\n"), code);
                    IO__close(socket);
                    return 1;
                }
            );
        },
        error(code, message)
        {
            IO__printf(&bstr("socket_test: bulk open error %i\n"), code);
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
    if (BString__equals(&mode, &bstr("bulk-listen")))
    {
        return SocketTest__bulk(argc, argv, true);
    }
    if (BString__equals(&mode, &bstr("bulk-connect")))
    {
        return SocketTest__bulk(argc, argv, false);
    }
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

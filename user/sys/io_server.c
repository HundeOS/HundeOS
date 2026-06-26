#include "io_server.h"

#include "syscall.h"

IOHandle io_server_register(const char * path, IOServerRole role)
{
    return syscall(IO_CALL, IO_SERVER_REGISTER, any(path), role, 0, 0);
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    return syscall(IO_CALL, IO_SERVER_GET_BUFFER, handle, any(buffer), 0, 0);
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    return syscall(IO_CALL, IO_SERVER_DATA_AVAILABLE, handle, byte_count, 0, 0);
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    return syscall(IO_CALL, IO_SERVER_DATA_CONSUMED, handle, byte_count, 0, 0);
}

IOStatus io_server_eof(IOHandle handle)
{
    return syscall(IO_CALL, IO_SERVER_EOF, handle, 0, 0, 0);
}

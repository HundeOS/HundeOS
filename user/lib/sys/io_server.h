#ifndef __USER_SYS_IO_SERVER_H
#define __USER_SYS_IO_SERVER_H

#include "../../common/types/bstring.h"
#include "syscalls/io.h"

IOHandle io_server_register(ConstBStringPtr path, IOServerRole role);
IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer);
IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count);
IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count);
IOStatus io_server_eof(IOHandle handle);

#endif

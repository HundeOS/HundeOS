#ifndef __USER_SYS_IO_H
#define __USER_SYS_IO_H

#include <stddef.h>
#include <stdint.h>

#include "syscalls/io.h"

IOHandle open(const char * path, Flags flags);
ByteCount read(IOHandle handle, char * buf, size_t size);
ByteCount write(IOHandle handle, char * buf, size_t size);
IOStatus close(IOHandle handle);

#endif
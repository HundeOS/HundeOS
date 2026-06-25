#ifndef __USER_SYS_IO_H
#define __USER_SYS_IO_H

#include <stddef.h>
#include <stdint.h>

#include "syscalls/io.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

IOHandle open(const char * path, Flags flags);
ByteCount read(IOHandle handle, char * buf, size_t size);
ByteCount write(IOHandle handle, char * buf, size_t size);
IOStatus close(IOHandle handle);
DirectoryEntryCount list_dir(const char * path, DirectoryEntryPtr entries, size_t max_entries);
IOStatus make_dir(const char * path);
IOStatus remove_file(const char * path);
IOStatus remove_file_with_flags(const char * path, Flags flags);

#endif

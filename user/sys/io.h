#ifndef __USER_SYS_IO_H
#define __USER_SYS_IO_H

#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "syscalls/io.h"

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

IOHandle open(const char * path, Flags flags);
ByteCount read(IOHandle handle, char * buf, size_t size);
ByteCount write(IOHandle handle, char * buf, size_t size);
IOHandle IO__open(ConstBStringPtr self, Flags flags);
BString IO__path_from_arg(ConstBStringPtr arg);
BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child);
ByteCount IO__read(IOHandle handle, BStringPtr self);
Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes);
ByteCount IO__write(IOHandle handle, ConstBStringPtr self);
IOStatus close(IOHandle handle);
DirectoryEntryCount list_dir(const char * path, DirectoryEntryPtr entries, size_t max_entries);
DirectoryEntryCount IO__list_dir(ConstBStringPtr self, DirectoryEntryPtr entries, size_t max_entries);
const char * io_status_string(IOStatus status);
IOStatus make_dir(const char * path);
IOStatus IO__make_dir(ConstBStringPtr self);
IOStatus remove_file(const char * path);
IOStatus IO__remove_file(ConstBStringPtr self);
IOStatus remove_file_with_flags(const char * path, Flags flags);
IOStatus IO__remove_file_with_flags(ConstBStringPtr self, Flags flags);
IOStatus link_file(const char * source_path, const char * target_path);
IOStatus IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path);
IOStatus mount_device(const char * device_path, const char * filesystem_type, const char * mountpoint);
IOStatus IO__mount_device(ConstBStringPtr device_path, ConstBStringPtr filesystem_type, ConstBStringPtr mountpoint);

#endif

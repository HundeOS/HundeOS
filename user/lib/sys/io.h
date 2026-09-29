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
void IO__set_startup_stdio(IOHandle input, IOHandle output, IOHandle error);
IOStatus IO__set_terminal_foreground(uint64_t pid);

IOHandle IO__open(ConstBStringPtr path, Flags flags);
ByteCount IO__read_bytes(IOHandle handle, char * buf, size_t size);
ByteCount IO__write_bytes(IOHandle handle, const char * buf, size_t size);
IOSeekResult IO__seek(IOHandle handle, int64_t offset, IOSeekWhence whence);
IOWatchResult IO__watch_file(IOHandle handle);
IOWatchResult IO__wait_file_change(IOFileWatch * watches, size_t count);
IOHandle accept(IOHandle handle);
IOHandle IO__accept(IOHandle handle);
BString IO__path_from_arg(ConstBStringPtr arg);
BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child);
ByteCount IO__read(IOHandle handle, BStringPtr buffer);
Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes);
ByteCount IO__write(IOHandle handle, ConstBStringPtr text);
IOStatus IO__close(IOHandle handle);
DirectoryEntryCount list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries);
DirectoryEntryCount IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries);
Result(IOFileInfo) IO__file_info(ConstBStringPtr path);
const char * io_status_string(IOStatus status);
IOStatus make_dir(ConstBStringPtr path);
IOStatus IO__make_dir(ConstBStringPtr path);
IOStatus remove_file(ConstBStringPtr path);
IOStatus IO__remove_file(ConstBStringPtr path);
IOStatus remove_file_with_flags(ConstBStringPtr path, Flags flags);
IOStatus IO__remove_file_with_flags(ConstBStringPtr path, Flags flags);
IOStatus link_file(ConstBStringPtr source_path, ConstBStringPtr target_path);
IOStatus IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path);

#endif

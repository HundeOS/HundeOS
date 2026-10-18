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
void IO__reset_after_fork(void);
Result(IOStatus) IO__set_terminal_foreground(uint64_t pid);
Optional(BString) IO__terminal_path(int descriptor);
Result(IOTerminalAttributes) IO__get_terminal_attributes(IOHandle handle);
Result(IOStatus) IO__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes);

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags);
Result(IOPipe) IO__pipe(void);
Result(ByteCount) IO__read_bytes(IOHandle handle, char * buf, size_t size);
Result(ByteCount) IO__write_bytes(IOHandle handle, const char * buf, size_t size);
Result(ByteCount) IO__read_bytes_with_flags(IOHandle handle, char * buf, size_t size, Flags flags);
Result(ByteCount) IO__write_bytes_with_flags(IOHandle handle, const char * buf, size_t size, Flags flags);
Result(IOPosition) IO__seek(IOHandle handle, int64_t offset, IOSeekWhence whence);
Result(IOFileChange) IO__watch_file(IOHandle handle);
Result(IOFileChange) IO__wait_file_change(IOFileWatch * watches, size_t count);
Result(IOHandle) accept(IOHandle handle);
Result(IOHandle) IO__accept(IOHandle handle);
BString IO__path_from_arg(ConstBStringPtr arg);
BString IO__path_join(ConstBStringPtr base, ConstBStringPtr child);
Result(ByteCount) IO__read(IOHandle handle, BStringPtr buffer);
Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes);
Result(ByteCount) IO__write(IOHandle handle, ConstBStringPtr text);
Result(IOStatus) IO__close(IOHandle handle);
Result(DirectoryEntryCount) list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries);
Result(DirectoryEntryCount) IO__list_dir(ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries);
Result(IOFileInfo) IO__file_info(ConstBStringPtr path);
const char * io_status_string(IOStatus status);
Result(IOStatus) make_dir(ConstBStringPtr path);
Result(IOStatus) IO__make_dir(ConstBStringPtr path);
Result(IOStatus) remove_file(ConstBStringPtr path);
Result(IOStatus) IO__remove_file(ConstBStringPtr path);
Result(IOStatus) remove_file_with_flags(ConstBStringPtr path, Flags flags);
Result(IOStatus) IO__remove_file_with_flags(ConstBStringPtr path, Flags flags);
Result(IOStatus) link_file(ConstBStringPtr source_path, ConstBStringPtr target_path);
Result(IOStatus) IO__link_file(ConstBStringPtr source_path, ConstBStringPtr target_path);

#endif

#ifndef HUND_IO_CLIENT_H
#define HUND_IO_CLIENT_H
#include "protocol.h"
IOHandle IOClient__open(ConstBStringPtr path, Flags flags);
IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path);
IOHandle IOClient__accept(IOHandle handle);
IOStatus IOClient__close(IOHandle handle);
ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size);
ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size);
IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence);
IOReply IOClient__watch_file(IOHandle handle);
IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count);
IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid);
IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset);
#endif

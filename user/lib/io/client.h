#ifndef HUND_IO_CLIENT_H
#define HUND_IO_CLIENT_H
#include "protocol.h"

typedef void * IOPathRouterContext;
typedef IOReply (*IOPathRoute)(IOPathRouterContext context, IORequestPtr request);

Result(IOStatus)
    IOClient__register_path_router(ConstBStringPtr service, IOPathRouterContext context, IOPathRoute route);
void IOClient__unregister_path_router(IOPathRouterContext context);

typedef struct IOClientInheritance
{
    IOHandle local;
    uint64_t token;
    Flags flags;
    uint64_t service_length;
    uint8_t service_data[IO_PROTOCOL_DATA_SIZE];
    bool single_transfer;
} IOClientInheritance;

typedef IOClientInheritance * IOClientInheritancePtr;
OptionalDef(IOClientInheritance);
ResultDef(IOClientInheritance);

Result(IOClientInheritance) IOClient__prepare_inheritance(IOHandle handle);
IOStatus IOClient__commit_inheritance(IOClientInheritancePtr inheritance, uint64_t child_pid);
IOStatus IOClient__abort_inheritance(IOClientInheritancePtr inheritance);
void IOClient__reset_after_fork(void);
IOStatus IOClient__claim_inheritance(IOClientInheritancePtr inheritance);
IOHandle IOClient__open(ConstBStringPtr path, Flags flags);
Result(IOPipe) IOClient__pipe(void);
IOStatus IOClient__link(ConstBStringPtr source_path, ConstBStringPtr target_path);
IOHandle IOClient__accept(IOHandle handle);
IOStatus IOClient__close(IOHandle handle);
ByteCount IOClient__read(IOHandle handle, ChannelDataPtr data, size_t size);
ByteCount IOClient__write(IOHandle handle, ConstChannelDataPtr data, size_t size);
ByteCount IOClient__read_with_flags(IOHandle handle, ChannelDataPtr data, size_t size, Flags flags);
ByteCount IOClient__write_with_flags(IOHandle handle, ConstChannelDataPtr data, size_t size, Flags flags);
IOReply IOClient__seek(IOHandle handle, int64_t offset, IOSeekWhence whence);
IOReply IOClient__watch_file(IOHandle handle);
IOReply IOClient__wait_file_change(IOFileWatch * watches, size_t count);
IOStatus IOClient__set_foreground_process(IOHandle handle, uint64_t pid);
IOTerminalAttributesResult IOClient__get_terminal_attributes(IOHandle handle);
IOStatus IOClient__set_terminal_attributes(IOHandle handle, IOTerminalAttributes attributes);
IOReply IOClient__path_request(IORequestOperation operation, ConstBStringPtr path, Flags flags, uint64_t offset);
#endif

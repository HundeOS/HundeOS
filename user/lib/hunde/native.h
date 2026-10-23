#ifndef HUNDE_RUST_NATIVE_H
#define HUNDE_RUST_NATIVE_H

#include "hunde/io_component.h"
#include "io/server.h"

#define HUNDE_LOCAL_IO_SESSION UINT64_MAX

typedef struct RustNativeLayout
{
    uint64_t size;
    uint64_t alignment;
} RustNativeLayout;

static inline RustNativeLayout RustNativeLayout__new(uint64_t size, uint64_t alignment)
{
    return (RustNativeLayout){.size = size, .alignment = alignment};
}

typedef struct RustAddressResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    void * address;
    HundeAbiError error;
} RustAddressResult;

typedef struct RustConsoleResult
{
    HundeAbiResultKind kind;
    uint32_t framebuffer;
    HundeAbiError error;
} RustConsoleResult;

typedef struct RustServiceEventResult
{
    HundeAbiResultKind kind;
    uint32_t event_type;
    Anything source;
    HundeAbiError error;
} RustServiceEventResult;

typedef struct RustServiceCompletion
{
    HundeIOCompletion completion;
    uint64_t redirect;
    uint64_t offset;
    uint64_t service_length;
    uint64_t path_length;
    uint64_t target_length;
    uint8_t service[IO_PROTOCOL_DATA_SIZE];
    uint8_t path[IO_PROTOCOL_DATA_SIZE];
    uint8_t target[IO_PROTOCOL_DATA_SIZE];
} RustServiceCompletion;

typedef RustServiceCompletion (*RustServiceExecute)(HundeComponentContext context, ConstHundeIORequestV6Ptr request);

typedef struct RustIOCallbacks
{
    HundeComponentContext context;
    RustServiceExecute execute;
    HundeIODisconnect disconnect;
} RustIOCallbacks;

typedef RustIOCallbacks * RustIOCallbacksPtr;
typedef const RustIOCallbacks * ConstRustIOCallbacksPtr;

typedef struct RustBlockOpenResult
{
    HundeAbiResultKind kind;
    uint32_t reserved;
    uint64_t sector_count;
    HundeAbiError error;
} RustBlockOpenResult;

typedef HundeStatusResult (*RustFiberEntry)(HundeHostContext context);

typedef struct RustFiberTask
{
    RustFiberEntry entry;
    HundeHostContext context;
} RustFiberTask;

typedef const RustFiberTask * ConstRustFiberTaskPtr;

static inline RustFiberTask RustFiberTask__new(RustFiberEntry entry, HundeHostContext context)
{
    return (RustFiberTask){.entry = entry, .context = context};
}

static inline HundeStatusResult RustNative__error(int32_t code)
{
    return HundeStatusResult__error(code, HundeByteSpan__new((const uint8_t *)"OS operation failed", 19));
}

static inline HundeStatusResult RustNative__unknown_error(void)
{
    return HundeStatusResult__error(RESULT_UNKNOWN_ERROR_CODE,
                                    HundeByteSpan__new((const uint8_t *)"Unknown error", 13));
}

Optional(IOReply)
    RustIOCallbacks__execute(IOServiceContext self, QueueConnectionID session, uint64_t pid, IORequestPtr request);
bool RustIOCallbacks__disconnect(IOServiceContext self, QueueConnectionID session, uint64_t pid);
IOReply RustIOCallbacks__execute_local(ConstRustIOCallbacksPtr self, uint64_t pid, IORequestPtr request);

RustNativeLayout hunde_rust_module_layout(void);
RustNativeLayout hunde_rust_embedfs_layout(void);
HundeStatusResult hunde_rust_embedfs_init(void * storage);
HundeIOCompletion hunde_rust_embedfs_execute(void * storage, ConstHundeIORequestV6Ptr input);
HundeStatusResult hunde_rust_embedfs_disconnect(void * storage, uint64_t session);
RustNativeLayout hunde_rust_block_layout(void);
RustBlockOpenResult hunde_rust_block_open(void * storage, HundeByteSpan path);
HundeStatusResult hunde_rust_block_read(void * storage, uint64_t sector, uint8_t * destination, uint64_t length);
HundeStatusResult hunde_rust_block_write(void * storage, uint64_t sector, const uint8_t * source, uint64_t length);
HundeStatusResult hunde_rust_block_close(void * storage);
void hunde_rust_block_reclaim(void * storage);
HundeCredentialsResult hunde_rust_credentials(uint64_t pid);
HundeProcessResult hunde_rust_process(uint64_t selector, uint32_t by_index);
HundeTraceResult hunde_rust_kernel_trace(uint8_t * destination, uint64_t capacity);
HundeStatusResult hunde_rust_module_load(void * storage, HundeByteSpan path);
RustAddressResult hunde_rust_module_export(void * storage, HundeByteSpan name);
HundeStatusResult hunde_rust_module_dispose(void * storage);
RustNativeLayout hunde_rust_service_layout(void);
HundeStatusResult hunde_rust_service_listen(void * storage, HundeByteSpan name, ConstRustIOCallbacksPtr callbacks);
bool hunde_rust_service_poll(void * storage);
void hunde_rust_service_serve_handles(void * storage);
HundeStatusResult hunde_rust_path_router_register(HundeByteSpan name, ConstRustIOCallbacksPtr callbacks);
void hunde_rust_path_router_unregister(ConstRustIOCallbacksPtr callbacks);
HundeStatusResult hunde_rust_become_service(void);
RustServiceEventResult hunde_rust_service_event(void);
RustConsoleResult hunde_rust_console_output(void);
HundeStatusResult hunde_rust_authorize_mount(uint64_t pid);
HundeStatusResult hunde_rust_publish_ready(HundeByteSpan name);
RustNativeLayout hunde_rust_scheduler_layout(void);
void hunde_rust_scheduler_init(void * storage);
HundeStatusResult hunde_rust_scheduler_spawn(void * storage, ConstRustFiberTaskPtr task);
HundeStatusResult hunde_rust_scheduler_run(void * storage);
HundeStatusResult hunde_rust_scheduler_park(void * storage);
HundeStatusResult hunde_rust_scheduler_wake(void * storage);
uint64_t hunde_rust_scheduler_live(void * storage);
uint64_t hunde_rust_scheduler_waiting(void * storage);
void hunde_rust_channel_prepare_wait(void);
void hunde_rust_channel_wait(void);
void hunde_rust_log(HundeByteSpan message);

_Static_assert(sizeof(RustNativeLayout) == 16, "Rust native layout ABI changed");
_Static_assert(sizeof(RustServiceEventResult) == 40, "Rust service event result ABI changed");
_Static_assert(offsetof(RustServiceEventResult, source) == 8, "Rust service event source offset changed");
_Static_assert(sizeof(RustFiberTask) == 16, "Rust fibre task ABI changed");
_Static_assert(offsetof(RustFiberTask, context) == 8, "Rust fibre context offset changed");
_Static_assert(sizeof(RustAddressResult) == 40, "Rust address result ABI changed");
_Static_assert(sizeof(RustIOCallbacks) == 24, "Rust I/O callbacks ABI changed");
_Static_assert(sizeof(RustBlockOpenResult) == 40, "Rust block open ABI changed");
_Static_assert(offsetof(RustBlockOpenResult, sector_count) == 8, "Rust block geometry offset changed");
_Static_assert(offsetof(RustBlockOpenResult, error) == 16, "Rust block error offset changed");

#endif

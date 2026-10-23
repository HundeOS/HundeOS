#include "native.h"
#include "sys/service.h"
#include "sys/process.h"
#include "sys/syslog.h"
#include "wuff/filesystem.h"
#include "sys/queue_wait.h"
#include "io/client.h"
#include "sys/boot_info.h"

static RustConsoleResult RustConsoleResult__new(bool framebuffer)
{
    return (RustConsoleResult){.kind = HUNDE_ABI_SUCCESS, .framebuffer = framebuffer};
}

static RustServiceEventResult RustServiceEventResult__new(ServiceEvent event)
{
    return (RustServiceEventResult){.kind = HUNDE_ABI_SUCCESS, .event_type = event.type, .source = event.source};
}

static RustServiceEventResult RustServiceEventResult__new_empty(void)
{
    return (RustServiceEventResult){.kind = HUNDE_IO_PENDING};
}

static RustServiceEventResult RustServiceEventResult__new_error(ServiceCode code)
{
    return (RustServiceEventResult){.kind = HUNDE_ABI_ERROR, .error = {.code = code}};
}

RustServiceEventResult hunde_rust_service_event(void)
{
    ServiceEvent event;
    ServiceCode code = service_poll(&event);
    if (code == SERVICE_EVENT_QUEUE_EMPTY)
    {
        return RustServiceEventResult__new_empty();
    }
    if (code != SERVICE_OK)
    {
        return RustServiceEventResult__new_error(code);
    }
    return RustServiceEventResult__new(event);
}

static RustConsoleResult RustConsoleResult__new_error(int32_t code)
{
    return (RustConsoleResult){.kind = HUNDE_ABI_ERROR, .error = {.code = code}};
}

RustConsoleResult hunde_rust_console_output(void)
{
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            return RustConsoleResult__new(BootHardwareInfo__has_framebuffer(&info));
        },
        error(code, message)
        {
            return RustConsoleResult__new_error(code);
        }
    );
    return RustConsoleResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

typedef struct RustService
{
    IOService service;
    RustIOCallbacks callbacks;
} RustService;

typedef RustService * RustServicePtr;

HundeStatusResult hunde_rust_authorize_mount(uint64_t pid)
{
    if (pid == 0 || !service_ok(service_authorize_mount_client(pid)))
    {
        return RustNative__error(IO_PERMISSION_DENIED);
    }
    return HundeStatusResult__success();
}

static HundeCredentialsResult HundeCredentialsResult__new_success(ProcessCredentials credentials)
{
    return (HundeCredentialsResult){
        .kind = HUNDE_ABI_SUCCESS, .user_id = credentials.user_id, .group_id = credentials.group_id};
}

static HundeCredentialsResult HundeCredentialsResult__new_error(int32_t code, HundeByteSpan message)
{
    return (HundeCredentialsResult){.kind = HUNDE_ABI_ERROR, .error = {.code = code, .message = message}};
}

HundeCredentialsResult hunde_rust_credentials(uint64_t pid)
{
    match_result(
        process_credentials(pid),
        success(credentials)
        {
            return HundeCredentialsResult__new_success(credentials);
        },
        error(code, message)
        {
            return HundeCredentialsResult__new_error(
                IO_UNKNOWN_PID, HundeByteSpan__new((const uint8_t *)"Credential lookup failed", 24));
        }
    );
    return HundeCredentialsResult__new_error(RESULT_UNKNOWN_ERROR_CODE,
                                             HundeByteSpan__new((const uint8_t *)"Unknown error", 13));
}

static RustAddressResult RustAddressResult__new_success(Address address)
{
    return (RustAddressResult){.kind = HUNDE_ABI_SUCCESS, .address = (void *)address};
}

static RustAddressResult RustAddressResult__new_error(int32_t code)
{
    return (RustAddressResult){.kind = HUNDE_ABI_ERROR,
                               .error = {.code = code, .message = {(const uint8_t *)"Unknown error", 13}}};
}

RustNativeLayout hunde_rust_module_layout(void)
{
    return RustNativeLayout__new(sizeof(WuffGraph), _Alignof(WuffGraph));
}

static HundeStatusResult RustNative__load_module(void * storage, HundeByteSpan path)
{
    WuffGraphPtr graph = storage;
    BString name = BString__from_chars((const char *)path.bytes, path.length);
    WuffMemoryOperations memory = WuffMemoryOperations__system();
    match_result(
        WuffFilesystem__load_module(&name, &memory),
        success(module)
        {
            *graph = module;
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            kernel_syslog_tag(&bstr("RUST"), &message);
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

HundeStatusResult hunde_rust_module_load(void * storage, HundeByteSpan path)
{
    static QueueWaitGate loader_gate = QUEUE_WAIT_GATE_INITIALIZER;
    match_result(
        QueueWaitGate__acquire(&loader_gate),
        success(value)
        {
            HundeStatusResult result = RustNative__load_module(storage, path);
            QueueWaitGate__release(&loader_gate);
            return result;
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

RustAddressResult hunde_rust_module_export(void * storage, HundeByteSpan name)
{
    BString symbol_name = BString__from_chars((const char *)name.bytes, name.length);
    match_result(
        WuffGraph__find_root_export(storage, &symbol_name),
        success(symbol)
        {
            return RustAddressResult__new_success(symbol.address);
        },
        error(code, message)
        {
            return RustAddressResult__new_error(code);
        }
    );
    return RustAddressResult__new_error(RESULT_UNKNOWN_ERROR_CODE);
}

HundeStatusResult hunde_rust_module_dispose(void * storage)
{
    match_result(
        WuffGraph__dispose(storage),
        success(value)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

RustNativeLayout hunde_rust_service_layout(void)
{
    return RustNativeLayout__new(sizeof(RustService), _Alignof(RustService));
}

HundeStatusResult hunde_rust_service_listen(void * storage, HundeByteSpan name, ConstRustIOCallbacksPtr callbacks)
{
    RustServicePtr self = storage;
    self->callbacks = *callbacks;
    BString service_name = BString__from_chars((const char *)name.bytes, name.length);
    IOStatus status = IOService__listen_into(&self->service, &service_name, &self->callbacks, RustIOCallbacks__execute,
                                             RustIOCallbacks__disconnect);
    if (status != IO_OK)
    {
        return RustNative__error(status);
    }
    return HundeStatusResult__success();
}

bool hunde_rust_service_poll(void * storage)
{
    RustServicePtr self = storage;
    return IOService__poll(&self->service);
}

void hunde_rust_service_serve_handles(void * storage)
{
    RustServicePtr self = storage;
    IOService__serve_handles(&self->service);
}

static IOReply RustLocalRouter__route(IOPathRouterContext context, IORequestPtr request)
{
    RustIOCallbacks callbacks = *(ConstRustIOCallbacksPtr)context;
    return RustIOCallbacks__execute_local(&callbacks, process_current_pid(), request);
}

HundeStatusResult hunde_rust_path_router_register(HundeByteSpan name, ConstRustIOCallbacksPtr callbacks)
{
    BString service = BString__from_chars((const char *)name.bytes, name.length);
    match_result(
        IOClient__register_path_router(&service, (IOPathRouterContext)callbacks, RustLocalRouter__route),
        success(status)
        {
            return HundeStatusResult__success();
        },
        error(code, message)
        {
            return RustNative__error(code);
        }
    );
    return RustNative__unknown_error();
}

void hunde_rust_path_router_unregister(ConstRustIOCallbacksPtr callbacks)
{
    IOClient__unregister_path_router((IOPathRouterContext)callbacks);
}

HundeStatusResult hunde_rust_become_service(void)
{
    if (!service_ok(service_become_service()))
    {
        return RustNative__error(IO_REGISTER_FAILED);
    }
    return HundeStatusResult__success();
}

HundeStatusResult hunde_rust_publish_ready(HundeByteSpan name)
{
    BString service_name = BString__from_chars((const char *)name.bytes, name.length);
    if (!service_ok(service_publish_ready(&service_name)))
    {
        return RustNative__error(IO_REGISTER_FAILED);
    }
    return HundeStatusResult__success();
}

void hunde_rust_channel_prepare_wait(void)
{
    Channel__prepare_wait();
}

void hunde_rust_channel_wait(void)
{
    Channel__wait_event();
}

void hunde_rust_log(HundeByteSpan message)
{
    BString text = BString__from_chars((const char *)message.bytes, message.length);
    kernel_syslog_tag(&bstr("RUST"), &text);
}

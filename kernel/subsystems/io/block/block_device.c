#include "block_device.h"

#include "panic.h"
#include "../../../all_defs.h"
#include "../../../cpu.h"
#include "../../../kalloc.h"
#include "../../../kernel_init.h"
#include "../../../process.h"
#include "../../../service/service.h"
#include "../../../scheduler/parked.h"
#include "../../../subsystems/message/io_operation_message.h"
#include "../../../subsystems/message/message_broker.h"
#include "../../../subsystems/message/subsystem_codes.h"
#include "../../../types/map.h"

static BString invalid_argument = BSTRING_NEW("Invalid block device argument");
static BString already_registered = BSTRING_NEW("Block device already registered");
static BString registry_full = BSTRING_NEW("Block device registry full");
static BString request_pending = BSTRING_NEW("Userspace block IO request pending");
static BString request_not_found = BSTRING_NEW("Userspace block IO request not found");
static BString request_failed = BSTRING_NEW("Userspace block IO request failed");
static BString request_not_complete = BSTRING_NEW("Userspace block IO request is not complete");

#define MAX_BLOCK_REQUESTS IO_MAX_DEVICES

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size) __attribute__((weak));
Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    return Error(UserAddress, invalid_argument, BLOCK_DEVICE_IO_ERROR);
}

typedef struct UserspaceBlockDevice
{
    BString alias;
    BString path;
    ProcessPtr service_process;
    MessageBrokerPtr message_broker;
    BlockCount block_count;
    BlockDevice block_device;
}
UserspaceBlockDevice;

typedef UserspaceBlockDevice * UserspaceBlockDevicePtr;

OptionalDef(UserspaceBlockDevicePtr);
FixedArrayBodyDefine(BlockDeviceMetadataEntries);
FixedArrayDefine(BlockRequestID, MAX_BLOCK_REQUESTS, BlockRequestIDs, SimpleEquals(uint64_t));
FixedArrayBodyDefine(BlockRequestIDs);

typedef struct UserspaceBlockRequest
{
    uint64_t request_id;
    UserspaceBlockDevicePtr device;
    ServiceBlockOperation operation;
    BlockNumber block;
    BlockCount block_count;
    UserAddress buffer;
    DataPtr caller_buffer;
    BlockIOContextPtr io_context;
    Optional(BlockIOContinuation) continuation;
    bool completed;
    ServiceCode status;
    BlockCount completed_blocks;
    Optional(ProcessID) waiting_pid;
}
UserspaceBlockRequest;

typedef UserspaceBlockRequest * UserspaceBlockRequestPtr;

typedef struct BlockDeviceParkContext
{
    KernelContext kernel_context;
    uint64_t request_id;
}
BlockDeviceParkContext;

typedef BlockDeviceParkContext * BlockDeviceParkContextPtr;

OptionalDef(UserspaceBlockRequestPtr);
FixedArrayBodyDefine(BlockDevicePaths);
MapDefine(BString, UserspaceBlockDevicePtr, IO_MAX_DEVICES, BlockDeviceRegistryMap, bstring_equals);
MapBodyDefine(BlockDeviceRegistryMap);
MapDefine(BlockRequestID, UserspaceBlockRequestPtr, MAX_BLOCK_REQUESTS, BlockRequestMap, SimpleEquals(uint64_t));
MapBodyDefine(BlockRequestMap);

static BlockDeviceRegistryMap registry = EmptyMap(BlockDeviceRegistryMap);
static BlockRequestMap requests = EmptyMap(BlockRequestMap);
static bool registry_initialized = false;
static uint64_t next_request_id = 1;
static uint64_t next_device_id = 0;
static Optional(KernelContextPtr) current_contexts[MAX_CPUS] = { 0 };
static BString hd_alias_format = BSTRING_NEW("hd%i");

static CPUID BlockDevice__current_context_index()
{
    CPUID id = CPU__id();
    if (id == 0)
    {
        return 0;
    }

    CPUID index = id - 1;
    if (index >= MAX_CPUS)
    {
        return 0;
    }

    return index;
}

static void UserspaceBlockDevice__copy_path(char target[SERVICE_DEVICE_PATH_MAX], BString source)
{
    BString__copy_to(&source, target, SERVICE_DEVICE_PATH_MAX);
}

static void BlockDeviceRegistry__init()
{
    if (registry_initialized)
    {
        return;
    }

    registry = BlockDeviceRegistryMap__new();
    requests = BlockRequestMap__new();
    next_device_id = 0;
    registry_initialized = true;
}

void BlockDeviceRegistry__reset(void)
{
    if (registry_initialized)
    {
        for_each_map(BlockDeviceRegistryMap, registry, index, path, device,
        {
            Kernel__free(addr(device));

        });
        for_each_map(BlockRequestMap, requests, request_index, request_id, request,
        {
            Kernel__free(addr(request));
        });
    }

    registry = BlockDeviceRegistryMap__new();
    requests = BlockRequestMap__new();
    next_device_id = 0;
    next_request_id = 1;
    registry_initialized = true;
}

bool BlockDeviceMetadata__equals(BlockDeviceMetadataPtr self, BlockDeviceMetadataPtr other)
{
    return BString__equals(&self->alias, &other->alias)
        && BString__equals(&self->path, &other->path)
        && self->block_size == other->block_size
        && self->block_count == other->block_count
        && self->userspace_service == other->userspace_service;
}

static Result(ServiceCode) UserspaceBlockDevice__deliver_service_event(
    UserspaceBlockDevicePtr self, ServiceEvent event
)
{
    MessagePtr message = Message__heap_new_service_event_delivery(
        SOURCE_SERVICE, SUBSYS_SERVICE_EVENT_AVAILABLE, self->service_process, event
    );

    match_result(AckCode, MessageBroker__deliver(self->message_broker, message),
        success(_) { return Success(ServiceCode, SERVICE_OK); },
        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
    );

    return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR);
}

static Result(Anything) UserspaceBlockDevice__complete_wait(ParkedOperationPtr parked_operation)
{
    BlockDeviceParkContextPtr block_context = (BlockDeviceParkContextPtr) parked_operation->kernel_context;

    match_optional(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, block_context->request_id),
        some(request)
        {
            if (!request->completed)
            {
                return Error(Anything, request_not_complete, BLOCK_DEVICE_REQUEST_PENDING);
            }

            if (request->status != SERVICE_OK)
            {
                return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
            }

            match_optional(BlockIOContinuation, request->continuation,
                some(continuation)
                {
                    request->io_context->buffer = ptr(request->buffer.kernel_addr.virtual_addr, Data);
                    request->io_context->buffer_size = request->block_count * BLOCK_DEVICE_BLOCK_SIZE;
                    request->io_context->completed_blocks = request->completed_blocks;
                    request->io_context->status = BLOCK_DEVICE_OK;
                    request->io_context->request_id = Some(BlockRequestID, request->request_id);
                    Result(Anything) continuation_result = continuation(request->io_context);
                    match_optional(KernelResultMapper, request->io_context->kernel_context.result_mapper,
                        some(mapper)
                        {
                            return mapper(
                            continuation_result,
                            request->io_context->kernel_context.result_mapper_context
                        ); },
                        none_do_nothing()
                    );
                    return continuation_result;
                },
                none_do_nothing()
            );

            Result(Anything) completed_blocks = Success(Anything, request->completed_blocks);
            match_optional(KernelResultMapper, block_context->kernel_context.result_mapper,
                some(mapper) { return mapper(completed_blocks, block_context->kernel_context.result_mapper_context); },
                none_do_nothing()
            );

            return completed_blocks;
        },
        none_do_nothing()
    );

    return Error(Anything, request_not_found, BLOCK_DEVICE_IO_ERROR);
}

static void UserspaceBlockDevice__park_for_request(UserspaceBlockRequestPtr request)
{
    Optional(KernelContextPtr) park_context = BlockDevice__current_context();
    match_optional(KernelContextPtr, park_context,
        some_do_nothing(),
        none()
        {
            if (request->io_context != 0)
            {
                park_context = Some(KernelContextPtr, &request->io_context->kernel_context);
            }
        }
    );

    match_optional(KernelContextPtr, park_context,
        some(context)
        {
            match_optional(ProcessID, context->pid,
                some(pid)
                {
                    __d_printf(
                        IODEV,
                        "block request park id=%i pid=%i io_context=%p continuation=%i\n",
                        (int) request->request_id,
                        (int) pid,
                        request->io_context,
                        (int) request->continuation.present
                    );

                    BlockDeviceParkContextPtr block_context =
                        ptr(Kernel__alloc_kernel(sizeof(BlockDeviceParkContext)), BlockDeviceParkContext);
                    block_context->kernel_context = KernelContext__new(Some(ProcessID, pid), context->scheduler);
                    block_context->request_id = request->request_id;
                    request->waiting_pid = Some(ProcessID, pid);

                    MessagePtr message = Message__heap_new_deferred_with_context_ownership(
                        SOURCE_IO,
                        SUBSYS_IO_PARK,
                        pid,
                        (KernelContextPtr) block_context,
                        UserspaceBlockDevice__complete_wait,
                        true
                    );
                    message->payload.some.val.deferred.operation_key = Some(OperationKey, request->request_id);

                    match_result(AckCode, MessageBroker__deliver(request->device->message_broker, message),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __d_printf(PANIC, "Park message failed %s\n", cstr(message));
                            Kernel__panic(cstr(message), NULL); // hundelint: ignore-no-null
                        }
                    );
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
}

static Result(BlockCount) UserspaceBlockDevice__create_request(
    UserspaceBlockDevicePtr self,
    ServiceBlockOperation operation,
    BlockNumber block,
    BlockCount count,
    DataPtr caller_buffer,
    BlockIOContextPtr io_context,
    Optional(BlockIOContinuation) continuation
)
{
    if (self == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (caller_buffer == NULL && is_nothing(continuation)) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (self->block_count != 0 && block + count > self->block_count)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    size_t buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    match_result(UserAddress, Process__alloc_user_pages(self->service_process, buffer_size),
        success(buffer)
        {
            if (operation == SERVICE_BLOCK_WRITE)
            {
                DataPtr kernel_buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
                for (size_t i = 0; i < buffer_size; i++)
                {
                    kernel_buffer[i] = caller_buffer[i];
                }
            }

            UserspaceBlockRequestPtr request =
                ptr(Kernel__alloc_kernel(sizeof(UserspaceBlockRequest)), UserspaceBlockRequest);
            request->request_id = next_request_id++;
            request->device = self;
            request->operation = operation;
            request->block = block;
            request->block_count = count;
            request->buffer = buffer;
            request->caller_buffer = caller_buffer;
            request->io_context = io_context;
            request->continuation = continuation;
            request->completed = false;
            request->status = SERVICE_EVENT_QUEUE_EMPTY;
            request->completed_blocks = 0;
            request->waiting_pid = None(ProcessID);

            match_result(CollectionCode, BlockRequestMap__set(&requests, request->request_id, request),
                success_do_nothing(),
                error(code, message) { return Error(BlockCount, registry_full, BLOCK_DEVICE_REGISTRY_FULL); }
            );

            __d_printf(
                IODEV,
                "block request create id=%i op=%i block=%i count=%i io_context=%p continuation=%i "
                "buffer_user=0x%x buffer_kernel=0x%x buffer_phys=0x%x size=%i\n",
                (int) request->request_id,
                (int) request->operation,
                (int) request->block,
                (int) request->block_count,
                request->io_context,
                (int) request->continuation.present,
                (unsigned int) request->buffer.virtual_addr,
                (unsigned int) request->buffer.kernel_addr.virtual_addr,
                (unsigned int) request->buffer.kernel_addr.phys_addr,
                (int) buffer_size
            );

            ServiceEvent event;
            event.type = SERVICE_EVENT_BLOCK_IO;
            event.source = request->request_id;
            event.data = 0;

            match_result(ServiceCode, UserspaceBlockDevice__deliver_service_event(self, event),
                success_do_nothing(),
                error(code, message) { return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR); }
            );

            UserspaceBlockDevice__park_for_request(request);

            return Error(BlockCount, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
        },
        error(code, message) { return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR); }
    );

    return Error(BlockCount, request_failed, BLOCK_DEVICE_IO_ERROR);
}

static Result(BlockCount) UserspaceBlockDevice__read(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target
)
{
    return UserspaceBlockDevice__create_request(
        (UserspaceBlockDevicePtr) context,
        SERVICE_BLOCK_READ,
        block,
        count,
        target,
        NULL, // hundelint: ignore-no-null
        None(BlockIOContinuation)
    );
}

static Result(BlockCount) UserspaceBlockDevice__write(
    BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source
)
{
    return UserspaceBlockDevice__create_request(
        (UserspaceBlockDevicePtr) context,
        SERVICE_BLOCK_WRITE,
        block,
        count,
        source,
        NULL, // hundelint: ignore-no-null
        None(BlockIOContinuation)
    );
}

BlockDevice BlockDevice__new(BlockDriverContextPtr context, BlockDeviceRead read, BlockDeviceWrite write)
{
    return (BlockDevice) {
        .context = context,
        .read = read,
        .write = write,
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .userspace_service = false,
    };
}

Result(BlockCount) BlockDevice__read(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr target)
{
    if (self == NULL || self->read == NULL || target == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return self->read(self->context, block, count, target);
}

Result(BlockCount) BlockDevice__write(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source)
{
    if (self == NULL || self->write == NULL || source == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return self->write(self->context, block, count, source);
}

Result(Anything) BlockDevice__read_async(
    BlockDevicePtr self, BlockNumber block, BlockCount count,
    BlockIOContextPtr context, BlockIOContinuation continuation
)
{
    if (self == NULL || context == NULL || continuation == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(Anything, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    context->block = block;
    context->block_count = count;
    context->completed_blocks = 0;
    context->status = BLOCK_DEVICE_REQUEST_PENDING;
    context->request_id = None(BlockRequestID);
    context->buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context) { context->kernel_context = *kernel_context; },
        none_do_nothing()
    );

    if (!self->userspace_service)
    {
        context->buffer = ptr(Kernel__alloc_kernel(context->buffer_size), Data);
        match_result(BlockCount, self->read(self->context, block, count, context->buffer),
            success(blocks)
            {
                context->completed_blocks = blocks;
                context->status = BLOCK_DEVICE_OK;
                return continuation(context);
            },
            error(code, message) { return Error(Anything, message, code); }
        );
    }

    match_result(UserAddress, Process__alloc_user_pages(((UserspaceBlockDevicePtr) self->context)->service_process, context->buffer_size),
        success(buffer)
        {
            UserspaceBlockDevicePtr userspace_device = (UserspaceBlockDevicePtr) self->context;
            UserspaceBlockRequestPtr request =
                ptr(Kernel__alloc_kernel(sizeof(UserspaceBlockRequest)), UserspaceBlockRequest);
            request->request_id = next_request_id++;
            request->device = userspace_device;
            request->operation = SERVICE_BLOCK_READ;
            request->block = block;
            request->block_count = count;
            request->buffer = buffer;
            request->caller_buffer = NULL; // hundelint: ignore-no-null
            request->io_context = context;
            request->continuation = Some(BlockIOContinuation, continuation);
            request->completed = false;
            request->status = SERVICE_EVENT_QUEUE_EMPTY;
            request->completed_blocks = 0;
            request->waiting_pid = None(ProcessID);

            context->buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
            context->request_id = Some(BlockRequestID, request->request_id);

            match_result(CollectionCode, BlockRequestMap__set(&requests, request->request_id, request),
                success_do_nothing(),
                error(code, message) { return Error(Anything, registry_full, BLOCK_DEVICE_REGISTRY_FULL); }
            );

            __d_printf(
                IODEV,
                "block async request create id=%i block=%i count=%i buffer_user=0x%x "
                "buffer_kernel=0x%x buffer_phys=0x%x size=%i io_context=%p\n",
                (int) request->request_id,
                (int) request->block,
                (int) request->block_count,
                (unsigned int) request->buffer.virtual_addr,
                (unsigned int) request->buffer.kernel_addr.virtual_addr,
                (unsigned int) request->buffer.kernel_addr.phys_addr,
                (int) context->buffer_size,
                request->io_context
            );

            ServiceEvent event;
            event.type = SERVICE_EVENT_BLOCK_IO;
            event.source = request->request_id;
            event.data = 0;

            match_result(ServiceCode, UserspaceBlockDevice__deliver_service_event(userspace_device, event),
                success_do_nothing(),
                error(code, message) { return Error(Anything, message, BLOCK_DEVICE_IO_ERROR); }
            );

            UserspaceBlockDevice__park_for_request(request);
            return Error(Anything, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
        },
        error(code, message) { return Error(Anything, message, BLOCK_DEVICE_IO_ERROR); }
    );

    return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
}

Result(Anything) BlockDevice__write_async(
    BlockDevicePtr self, BlockNumber block, BlockCount count,
    DataPtr source, BlockIOContextPtr context, BlockIOContinuation continuation
)
{
    if (self == NULL || source == NULL || context == NULL || continuation == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(Anything, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    context->block = block;
    context->block_count = count;
    context->buffer = source;
    context->buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    context->completed_blocks = 0;
    context->status = BLOCK_DEVICE_REQUEST_PENDING;
    context->request_id = None(BlockRequestID);
    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context) { context->kernel_context = *kernel_context; },
        none_do_nothing()
    );

    if (self->userspace_service)
    {
        match_result(BlockCount, UserspaceBlockDevice__create_request(
            (UserspaceBlockDevicePtr) self->context,
            SERVICE_BLOCK_WRITE,
            block,
            count,
            source,
            context,
            Some(BlockIOContinuation, continuation)
        ),
            success(blocks) { return Success(Anything, blocks); },
            error(code, message) { return Error(Anything, message, code); }
        );
    }

    match_result(BlockCount, self->write(self->context, block, count, source),
        success(blocks)
        {
            context->completed_blocks = blocks;
            context->status = blocks == count ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO_ERROR;
            return continuation(context);
        },
        error(code, message) { return Error(Anything, message, code); }
    );

    return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
}

void BlockDevice__set_current_context(KernelContextPtr context)
{
    current_contexts[BlockDevice__current_context_index()] = Some(KernelContextPtr, context);
}

void BlockDevice__clear_current_context()
{
    current_contexts[BlockDevice__current_context_index()] = None(KernelContextPtr);
}

Optional(KernelContextPtr) BlockDevice__current_context()
{
    return current_contexts[BlockDevice__current_context_index()];
}

Result(BlockDeviceCode) BlockDeviceRegistry__register(
    BString path, ProcessPtr service_process, BlockCount block_count, MessageBrokerPtr message_broker
)
{
    if (BString__is_empty(&path) || service_process == NULL || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockDeviceRegistry__init();

    match_optional(UserspaceBlockDevicePtr, BlockDeviceRegistryMap__get(&registry, path),
        some(existing) { return Error(BlockDeviceCode, already_registered, BLOCK_DEVICE_ALREADY_REGISTERED); },
        none_do_nothing()
    );

    UserspaceBlockDevicePtr device = ptr(Kernel__alloc_kernel(sizeof(UserspaceBlockDevice)), UserspaceBlockDevice);
    *device = (UserspaceBlockDevice) {
        .alias = BString__sprintf(&hd_alias_format, (int) next_device_id++),
        .path = path,
        .service_process = service_process,
        .message_broker = message_broker,
        .block_count = block_count,
    };
    device->block_device = BlockDevice__new(device, UserspaceBlockDevice__read, UserspaceBlockDevice__write);
    device->block_device.userspace_service = true;

    match_result(CollectionCode, BlockDeviceRegistryMap__set(&registry, path, device),
        success(_) { return Success(BlockDeviceCode, BLOCK_DEVICE_OK); },
        error(code, message) { return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL); }
    );

    return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
}

Result(BlockDeviceCode) BlockDeviceRegistry__dispose_process_resources(ProcessPtr process)
{
    if (process == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockDeviceRegistry__init();

    BlockRequestIDs stale_request_ids = BlockRequestIDs__new();
    for_each_map(BlockRequestMap, requests, index, request_id, request,
    {
        if (request->device->service_process == process)
        {
            match_result(CollectionCode, BlockRequestIDs__push(&stale_request_ids, request_id),
                success_do_nothing(),
                error(code, message) { return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL); }
            );
        }

    });
    for_each_fa(BlockRequestIDs, stale_request_ids, request_index, stale_request_id,
    {
        match_optional(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, stale_request_id),
            some(request) { Kernel__free(addr(request)); },
            none_do_nothing()
        );
        BlockRequestMap__delete(&requests, stale_request_id);

    });
    BlockDevicePaths stale_paths = EmptyArray(BlockDevicePaths);
    for_each_map(BlockDeviceRegistryMap, registry, index, path, device,
    {
        if (device->service_process == process)
        {
            match_result(CollectionCode, BlockDevicePaths__push(&stale_paths, path),
                success_do_nothing(),
                error(code, message) { return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL); }
            );
        }

    });
    for_each_fa(BlockDevicePaths, stale_paths, path_index, stale_path,
    {
        match_optional(UserspaceBlockDevicePtr, BlockDeviceRegistryMap__get(&registry, stale_path),
            some(device) { Kernel__free(addr(device)); },
            none_do_nothing()
        );
        BlockDeviceRegistryMap__delete(&registry, stale_path);

    });
    return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
}

Result(ServiceCode) BlockDeviceService__get_request(
    ProcessPtr service_process, uint64_t request_id, ServiceBlockRequestPtr target
)
{
    if (service_process == NULL || target == NULL) // hundelint: ignore-no-null
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    BlockDeviceRegistry__init();

    match_optional(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, request_id),
        some(request)
        {
            if (request->device->service_process != service_process)
            {
                return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
            }

            target->request_id = request->request_id;
            UserspaceBlockDevice__copy_path(target->device_path, request->device->path);
            target->operation = request->operation;
            target->block = request->block;
            target->block_count = request->block_count;
            target->block_size = BLOCK_DEVICE_BLOCK_SIZE;
            target->buffer = ptr(request->buffer.virtual_addr, void);
            target->buffer_size = request->block_count * BLOCK_DEVICE_BLOCK_SIZE;
            return Success(ServiceCode, SERVICE_OK);
        },
        none_do_nothing()
    );

    return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
}

Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
)
{
    if (service_process == NULL || completion == NULL) // hundelint: ignore-no-null
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    BlockDeviceRegistry__init();

    match_optional(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, completion->request_id),
        some(request)
        {
            if (request->device->service_process != service_process)
            {
                return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
            }

            if (completion->block_count > request->block_count)
            {
                return Error(ServiceCode, request_failed, SERVICE_BLOCK_REQUEST_FAILED);
            }

            if (completion->status == SERVICE_OK && request->operation == SERVICE_BLOCK_READ && request->caller_buffer != NULL) // hundelint: ignore-no-null
            {
                size_t byte_count = completion->block_count * BLOCK_DEVICE_BLOCK_SIZE;
                DataPtr kernel_buffer = ptr(request->buffer.kernel_addr.virtual_addr, Data);
                for (size_t i = 0; i < byte_count; i++)
                {
                    request->caller_buffer[i] = kernel_buffer[i];
                }
            }

            request->completed = true;
            request->status = completion->status;
            request->completed_blocks = completion->block_count;

            __d_printf(
                IODEV,
                "block request complete id=%i op=%i status=%i completed=%i waiting=%i\n",
                (int) request->request_id,
                (int) request->operation,
                (int) request->status,
                (int) request->completed_blocks,
                (int) request->waiting_pid.present
            );

            match_optional(ProcessID, request->waiting_pid,
                some(pid)
                {
                    MessagePtr message = Message__heap_new_io_operation(
                        SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, request->request_id
                    );
                    match_result(AckCode, MessageBroker__deliver(request->device->message_broker, message),
                        success_do_nothing(),
                        error(code, message)
                        {
                            return Error(ServiceCode, message, code);
                        }
                    );
                },
                none_do_nothing()
            );

            return Success(ServiceCode, SERVICE_OK);
        },
        none_do_nothing()
    );

    return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
}

Optional(BlockDevicePtr) BlockDeviceRegistry__find(BString path)
{
    BlockDeviceRegistry__init();

    match_optional(UserspaceBlockDevicePtr, BlockDeviceRegistryMap__get(&registry, path),
        some(device) { return Some(BlockDevicePtr, &device->block_device); },
        none_do_nothing()
    );

    return None(BlockDevicePtr);
}

BlockDevicePaths BlockDeviceRegistry__paths()
{
    BlockDeviceRegistry__init();

    BlockDevicePaths paths = EmptyArray(BlockDevicePaths);
    for_each_map(BlockDeviceRegistryMap, registry, index, path, device,
    {
        BlockDevicePaths__push(&paths, path);

    });
    return paths;
}

BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries()
{
    BlockDeviceRegistry__init();

    BlockDeviceMetadataEntries entries = EmptyArray(BlockDeviceMetadataEntries);
    for_each_map(BlockDeviceRegistryMap, registry, index, path, device,
    {
        BlockDeviceMetadataEntries__push(&entries, ((BlockDeviceMetadata) {
            .alias = device->alias,
            .path = device->path,
            .block_size = device->block_device.block_size,
            .block_count = device->block_count,
            .userspace_service = device->block_device.userspace_service,
        }));

    });
    return entries;
}

Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(BString alias)
{
    BlockDeviceRegistry__init();

    for_each_map(BlockDeviceRegistryMap, registry, index, path, device,
    {
        if (BString__equals(&device->alias, &alias))
        {
            return Some(BlockDeviceMetadata, ((BlockDeviceMetadata) {
                .alias = device->alias,
                .path = device->path,
                .block_size = device->block_device.block_size,
                .block_count = device->block_count,
                .userspace_service = device->block_device.userspace_service,
            }));
        }

    });
    return None(BlockDeviceMetadata);
}

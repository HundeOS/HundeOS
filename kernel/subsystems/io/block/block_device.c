#include "block_device.h"

#include "panic.h"
#include "../../../all_defs.h"
#include "../../../cpu.h"
#include "../../../kalloc.h"
#include "../../../kernel_core.h"
#include "../../../kernel_init.h"
#include "../../../process.h"
#include "../../../service/service.h"
#include "../../../scheduler/parked.h"
#include "../../../subsystems/message/io_operation_message.h"
#include "../../../subsystems/message/message_broker.h"
#include "../../../subsystems/message/subsystem_codes.h"
#include "../../../types/map.h"

static BString invalid_argument = bstr("Invalid block device argument");
static BString already_registered = bstr("Block device already registered");
static BString registry_full = bstr("Block device registry full");
static BString request_pending = bstr("Userspace block IO request pending");
static BString request_not_found = bstr("Userspace block IO request not found");
static BString request_failed = bstr("Userspace block IO request failed");
static BString request_not_complete = bstr("Userspace block IO request is not complete");
static BString allocation_failed = bstr("Userspace block IO allocation failed");

#define MAX_BLOCK_REQUESTS IO_MAX_DEVICES
#define MAX_BLOCK_CHANNELS IO_MAX_DEVICES

typedef struct UserspaceBlockDevice
{
    BString alias;
    BString path;
    ProcessPtr service_process;
    MessageBrokerPtr message_broker;
    BlockCount block_count;
    BlockDevice block_device;
} UserspaceBlockDevice;

typedef UserspaceBlockDevice * UserspaceBlockDevicePtr;

OptionalDef(UserspaceBlockDevicePtr);
OptionalDef(DataPtr);
OptionalDef(BlockIOContextPtr);
FixedArrayBodyDefine(BlockDeviceMetadataEntries);
FixedArrayDefine(BlockRequestID, MAX_BLOCK_REQUESTS, BlockRequestIDs, SimpleEquals(uint64_t));
FixedArrayBodyDefine(BlockRequestIDs);

typedef uint64_t BlockChannelID;

OptionalDef(BlockChannelID);

typedef struct UserspaceBlockChannel
{
    BlockChannelID channel_id;
    UserspaceBlockDevicePtr device;
    ProcessPtr requester;
    UserAddress requester_allocation;
    UserAddress service_allocation;
} UserspaceBlockChannel;

typedef UserspaceBlockChannel * UserspaceBlockChannelPtr;

OptionalDef(UserspaceBlockChannelPtr);
FixedArrayDefine(UserspaceBlockChannelPtr, MAX_BLOCK_CHANNELS, BlockChannels, Ptr__equals);
FixedArrayBodyDefine(BlockChannels);
FixedArrayDefine(UserspaceBlockDevicePtr, IO_MAX_DEVICES, UserspaceBlockDevices, Ptr__equals);
FixedArrayBodyDefine(UserspaceBlockDevices);

typedef struct UserspaceBlockRequest
{
    uint64_t request_id;
    UserspaceBlockDevicePtr device;
    ServiceBlockOperation operation;
    BlockNumber block;
    BlockCount block_count;
    UserAddress buffer;
    UserAddress buffer_allocation;
    Optional(DataPtr) caller_buffer;
    Optional(BlockIOContextPtr) io_context;
    Optional(BlockIOContinuation) continuation;
    bool completed;
    ServiceCode status;
    BlockCount completed_blocks;
    Optional(ProcessID) waiting_pid;
    Optional(ProcessPtr) response_process;
} UserspaceBlockRequest;

typedef UserspaceBlockRequest * UserspaceBlockRequestPtr;

typedef struct BlockDeviceParkContext
{
    KernelContext kernel_context;
    uint64_t request_id;
} BlockDeviceParkContext;

typedef BlockDeviceParkContext * BlockDeviceParkContextPtr;

OptionalDef(UserspaceBlockRequestPtr);
OptionalDef(MessageBrokerPtr);
ResultDef(UserspaceBlockDevicePtr);
ResultDef(UserspaceBlockRequestPtr);
ResultDef(UserspaceBlockChannelPtr);
ResultDef(BlockDeviceParkContextPtr);
typedef struct BlockChannelOpenMemory
{
    BlockChannelID channel_id;
    UserAddress requester_allocation;
    UserAddress service_allocation;
    UserAddress service_mapping;
    size_t size;
} BlockChannelOpenMemory;

ResultDef(BlockChannelOpenMemory);
FixedArrayBodyDefine(BlockDevicePaths);
FixedArrayDefine(UserspaceBlockRequestPtr, MAX_BLOCK_REQUESTS, BlockRequests, Ptr__equals);
FixedArrayBodyDefine(BlockRequests);
MapDefine(BString, UserspaceBlockDevicePtr, IO_MAX_DEVICES, BlockDeviceRegistryMap, bstring_equals);
MapBodyDefine(BlockDeviceRegistryMap);
MapDefine(BlockRequestID, UserspaceBlockRequestPtr, MAX_BLOCK_REQUESTS, BlockRequestMap, SimpleEquals(uint64_t));
MapBodyDefine(BlockRequestMap);
MapDefine(BlockChannelID, UserspaceBlockChannelPtr, MAX_BLOCK_CHANNELS, BlockChannelMap, SimpleEquals(uint64_t));
MapBodyDefine(BlockChannelMap);

typedef struct BlockDeviceRegistryState
{
    mutex_t lock;
    BlockDeviceRegistryMap devices;
    BlockRequestMap requests;
    BlockChannelMap channels;
    uint64_t next_request_id;
    uint64_t next_device_id;
    uint64_t next_channel_id;
} BlockDeviceRegistryState;

#define BLOCK_DEVICE_REGISTRY_STATE_INIT                                                                               \
    (BlockDeviceRegistryState)                                                                                         \
    {                                                                                                                  \
        .lock = MUTEX_INIT, .devices = EmptyMap(BlockDeviceRegistryMap), .requests = EmptyMap(BlockRequestMap),        \
        .channels = EmptyMap(BlockChannelMap), .next_request_id = 1, .next_device_id = 0, .next_channel_id = 1,        \
    }

static BlockDeviceRegistryState registry = BLOCK_DEVICE_REGISTRY_STATE_INIT;
static Optional(KernelContextPtr) current_contexts[MAX_CPUS] = {0};
static BString hd_alias_format = bstr("hd%i");

static BlockChannelOpenMemory BlockChannelOpenMemory__new(BlockChannelID channel_id,
                                                          UserAddressPtr requester_allocation,
                                                          UserAddressPtr service_allocation,
                                                          UserAddressPtr service_mapping, size_t size)
{
    return (BlockChannelOpenMemory){
        .channel_id = channel_id,
        .requester_allocation = *requester_allocation,
        .service_allocation = *service_allocation,
        .service_mapping = *service_mapping,
        .size = size,
    };
}

static Result(BlockCount)
    UserspaceBlockDevice__read(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target);
static Result(BlockCount)
    UserspaceBlockDevice__write(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source);
static Result(ServiceCode)
    UserspaceBlockDevice__deliver_service_event(UserspaceBlockDevicePtr self, ProcessPtr target, ServiceEvent event);

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

static UserspaceBlockDevice UserspaceBlockDevice__new(ConstBStringPtr alias, ConstBStringPtr path,
                                                      ProcessPtr service_process, MessageBrokerPtr message_broker,
                                                      BlockCount block_count, const BlockDevice * block_device)
{
    return (UserspaceBlockDevice){
        .alias = *alias,
        .path = *path,
        .service_process = service_process,
        .message_broker = message_broker,
        .block_count = block_count,
        .block_device = *block_device,
    };
}

static BlockDevice UserspaceBlockDevice__block_device(UserspaceBlockDevicePtr self)
{
    BlockDevice block_device = BlockDevice__new(self, UserspaceBlockDevice__read, UserspaceBlockDevice__write);
    block_device.userspace_service = true;
    return block_device;
}

static UserspaceBlockChannel UserspaceBlockChannel__new(BlockChannelID channel_id, UserspaceBlockDevicePtr device,
                                                        ProcessPtr requester, UserAddressPtr requester_allocation,
                                                        UserAddressPtr service_allocation)
{
    return (UserspaceBlockChannel){
        .channel_id = channel_id,
        .device = device,
        .requester = requester,
        .requester_allocation = *requester_allocation,
        .service_allocation = *service_allocation,
    };
}

static Result(UserspaceBlockDevicePtr)
    UserspaceBlockDevice__heap_new(ConstBStringPtr alias, ConstBStringPtr path, ProcessPtr service_process,
                                   MessageBrokerPtr message_broker, BlockCount block_count)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceBlockDevice));
    if (!address.valid)
    {
        return Error(UserspaceBlockDevicePtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
    }

    UserspaceBlockDevicePtr self = ptr(address.virtual_addr, UserspaceBlockDevice);
    BlockDevice block_device = UserspaceBlockDevice__block_device(self);
    *self = UserspaceBlockDevice__new(alias, path, service_process, message_broker, block_count, &block_device);
    return Success(UserspaceBlockDevicePtr, self);
}

static Result(UserspaceBlockChannelPtr)
    UserspaceBlockChannel__heap_new(BlockChannelID channel_id, UserspaceBlockDevicePtr device, ProcessPtr requester,
                                    UserAddressPtr requester_allocation, UserAddressPtr service_allocation)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceBlockChannel));
    if (!address.valid)
    {
        return Error(UserspaceBlockChannelPtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
    }

    UserspaceBlockChannelPtr self = ptr(address.virtual_addr, UserspaceBlockChannel);
    *self = UserspaceBlockChannel__new(channel_id, device, requester, requester_allocation, service_allocation);
    return Success(UserspaceBlockChannelPtr, self);
}

static UserspaceBlockRequest
UserspaceBlockRequest__new(uint64_t request_id, UserspaceBlockDevicePtr device, ServiceBlockOperation operation,
                           BlockNumber block, BlockCount block_count, UserAddress buffer, UserAddress buffer_allocation,
                           OptionalPtr(DataPtr) caller_buffer, OptionalPtr(BlockIOContextPtr) io_context,
                           OptionalPtr(BlockIOContinuation) continuation)
{
    return (UserspaceBlockRequest){
        .request_id = request_id,
        .device = device,
        .operation = operation,
        .block = block,
        .block_count = block_count,
        .buffer = buffer,
        .buffer_allocation = buffer_allocation,
        .caller_buffer = *caller_buffer,
        .io_context = *io_context,
        .continuation = *continuation,
        .completed = false,
        .status = SERVICE_EVENT_QUEUE_EMPTY,
        .completed_blocks = 0,
        .waiting_pid = None(ProcessID),
        .response_process = None(ProcessPtr),
    };
}

static Result(UserspaceBlockRequestPtr)
    UserspaceBlockRequest__heap_new(UserspaceBlockDevicePtr device, ServiceBlockOperation operation, BlockNumber block,
                                    BlockCount block_count, UserAddress buffer, UserAddress buffer_allocation,
                                    OptionalPtr(DataPtr) caller_buffer, OptionalPtr(BlockIOContextPtr) io_context,
                                    OptionalPtr(BlockIOContinuation) continuation)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceBlockRequest));
    if (!address.valid)
    {
        return Error(UserspaceBlockRequestPtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
    }

    UserspaceBlockRequestPtr self = ptr(address.virtual_addr, UserspaceBlockRequest);
    *self = UserspaceBlockRequest__new(0, device, operation, block, block_count, buffer, buffer_allocation,
                                       caller_buffer, io_context, continuation);
    return Success(UserspaceBlockRequestPtr, self);
}

static BlockDeviceParkContext BlockDeviceParkContext__new(ProcessID pid, OptionalPtr(SchedulerPtr) scheduler,
                                                          uint64_t request_id)
{
    return (BlockDeviceParkContext){
        .kernel_context = KernelContext__new(&Some(ProcessID, pid), scheduler),
        .request_id = request_id,
    };
}

static Result(BlockDeviceParkContextPtr)
    BlockDeviceParkContext__heap_new(ProcessID pid, OptionalPtr(SchedulerPtr) scheduler, uint64_t request_id)
{
    KernelAddress address = Kernel__alloc(sizeof(BlockDeviceParkContext));
    if (!address.valid)
    {
        return Error(BlockDeviceParkContextPtr, allocation_failed, BLOCK_DEVICE_IO_ERROR);
    }

    BlockDeviceParkContextPtr self = ptr(address.virtual_addr, BlockDeviceParkContext);
    *self = BlockDeviceParkContext__new(pid, scheduler, request_id);
    return Success(BlockDeviceParkContextPtr, self);
}

static ServiceEvent UserspaceBlockRequest__service_event(UserspaceBlockRequestPtr self)
{
    return ServiceEvent__new(SERVICE_EVENT_BLOCK_IO, self->request_id, 0);
}

static ServiceBlockRequest UserspaceBlockRequest__service_request(UserspaceBlockRequestPtr self)
{
    return ServiceBlockRequest__new(self->request_id, self->device->path, self->operation, self->block,
                                    self->block_count, BLOCK_DEVICE_BLOCK_SIZE, self->buffer.virtual_addr,
                                    self->block_count * BLOCK_DEVICE_BLOCK_SIZE);
}

static bool UserspaceBlockRequest__has_continuation(UserspaceBlockRequestPtr self)
{
    bool result = false;
    match_optional(self->continuation,
        some(_) { result = true; },
        none_do_nothing()
    );
    return result;
}

static bool BlockIOContinuation__is_some(OptionalPtr(BlockIOContinuation) continuation)
{
    bool result = false;
    match_optional(*continuation,
        some(_) { result = true; },
        none_do_nothing()
    );
    return result;
}

static BlockDeviceMetadata BlockDeviceMetadata__from_device(UserspaceBlockDevicePtr self)
{
    return BlockDeviceMetadata__new(&self->alias, &self->path, self->block_device.block_size, self->block_count,
                                    self->block_device.userspace_service);
}

static void UserspaceBlockRequest__release(UserspaceBlockRequestPtr self)
{
    match_result(
        Process__free_user_pages(self->device->service_process, self->buffer_allocation.virtual_addr),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(IODEV, "block request free failed id=%i code=%i\n", (int)self->request_id, (int)code);
        }
    );

    Kernel__free(addr(self));
}

static Result(ServiceCode) UserspaceBlockChannel__notify_closed(UserspaceBlockChannelPtr self)
{
    return UserspaceBlockDevice__deliver_service_event(
        self->device, self->device->service_process,
        ServiceEvent__new(SERVICE_EVENT_BLOCK_CHANNEL_CLOSED, self->channel_id, 0));
}

static void UserspaceBlockChannel__free_mappings(UserspaceBlockChannelPtr self)
{
    match_result(
        Process__free_user_pages(self->device->service_process, self->service_allocation.virtual_addr),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(IODEV, "block channel service unmap failed id=%i code=%i\n", (int)self->channel_id, (int)code);
        }
    );

    match_result(
        Process__free_user_pages(self->requester, self->requester_allocation.virtual_addr),
        success_do_nothing(),
        error(code, message)
        {
            __d_printf(IODEV, "block channel requester free failed id=%i code=%i\n", (int)self->channel_id, (int)code);
        }
    );
}

static Result(ServiceCode) UserspaceBlockChannel__release(UserspaceBlockChannelPtr self, bool notify_service)
{
    if (notify_service)
    {
        match_result(
            UserspaceBlockChannel__notify_closed(self),
            success_do_nothing(),
            error(code, message)
            {
                __d_printf(IODEV, "block channel close notify failed id=%i code=%i\n", (int)self->channel_id,
                           (int)code);
            }
        );
    }

    UserspaceBlockChannel__free_mappings(self);
    Kernel__free(addr(self));
    return Success(ServiceCode, SERVICE_OK);
}

static void BlockDeviceRegistry__remove_request(UserspaceBlockRequestPtr request)
{
    critical_section(registry.lock, match_result(
                                        BlockRequestMap__delete(&registry.requests, request->request_id),
                                        success_do_nothing(),
        error(code, message)
        {
                                            __d_printf(IODEV, "block request delete failed id=%i code=%i\n",
                                                       (int)request->request_id, (int)code);
                                        }
    ););
}

void BlockDeviceRegistry__reset(void)
{
    BlockRequests stale_requests = BlockRequests__new();
    BlockChannels stale_channels = BlockChannels__new();
    UserspaceBlockDevices stale_devices = UserspaceBlockDevices__new();

    critical_section(
        registry.lock, for_each_map(BlockDeviceRegistryMap, registry.devices, index, path, device,
                                    {
                                        match_result(
                                            UserspaceBlockDevices__push(&stale_devices, device),
                                            success_do_nothing(),
                                            error(code, message)
                                            {
                                                __d_printf(IODEV, "block device reset collect failed code=%i\n",
                                                           (int)code);
                                            }
                                        );
                                    });
        for_each_map(BlockRequestMap, registry.requests, request_index, request_id, request,
                     {
                         match_result(
                             BlockRequests__push(&stale_requests, request),
                             success_do_nothing(),
                             error(code, message)
                             {
                                 __d_printf(IODEV, "block request reset collect failed code=%i\n", (int)code);
                             }
                         );
                     });
        for_each_map(BlockChannelMap, registry.channels, channel_index, channel_id, channel,
                     {
                         match_result(
                             BlockChannels__push(&stale_channels, channel),
                             success_do_nothing(),
                             error(code, message)
                             {
                                 __d_printf(IODEV, "block channel reset collect failed code=%i\n", (int)code);
                             }
                         );
                     });

        registry.devices = BlockDeviceRegistryMap__new(); registry.requests = BlockRequestMap__new();
        registry.channels = BlockChannelMap__new(); registry.next_device_id = 0; registry.next_request_id = 1;
        registry.next_channel_id = 1;);

    for_each_fa(BlockChannels, stale_channels, channel_index, channel,
                { UserspaceBlockChannel__release(channel, false); });

    for_each_fa(BlockRequests, stale_requests, request_index, request, { UserspaceBlockRequest__release(request); });

    for_each_fa(UserspaceBlockDevices, stale_devices, device_index, device, { Kernel__free(addr(device)); });
}

BlockDeviceMetadata BlockDeviceMetadata__new(ConstBStringPtr alias, ConstBStringPtr path, size_t block_size,
                                             BlockCount block_count, bool userspace_service)
{
    return (BlockDeviceMetadata){
        .alias = *alias,
        .path = *path,
        .block_size = block_size,
        .block_count = block_count,
        .userspace_service = userspace_service,
    };
}

bool BlockDeviceMetadata__equals(BlockDeviceMetadataPtr self, BlockDeviceMetadataPtr other)
{
    return BString__equals(&self->alias, &other->alias) && BString__equals(&self->path, &other->path) &&
           self->block_size == other->block_size && self->block_count == other->block_count &&
           self->userspace_service == other->userspace_service;
}

static Result(ServiceCode)
    UserspaceBlockDevice__deliver_service_event(UserspaceBlockDevicePtr self, ProcessPtr target, ServiceEvent event)
{
    MessagePtr message =
        Message__heap_new_service_event_delivery(SOURCE_SERVICE, SUBSYS_SERVICE_EVENT_AVAILABLE, target, event);

    match_result(
        MessageBroker__deliver(self->message_broker, message),
        success(_) { return Success(ServiceCode, SERVICE_OK); },
        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
    );

    return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR);
}

static Result(Anything) UserspaceBlockDevice__complete_wait(ParkedOperationPtr parked_operation)
{
    BlockDeviceParkContextPtr block_context = (BlockDeviceParkContextPtr)parked_operation->kernel_context;
    Optional(UserspaceBlockRequestPtr) completed_request = None(UserspaceBlockRequestPtr);
    Result(Anything) wait_result = Error(Anything, request_not_found, BLOCK_DEVICE_IO_ERROR);

    critical_section(registry.lock,
                     match_optional(
                         BlockRequestMap__get(&registry.requests, block_context->request_id),
                         some(request)
                         {
                             if (!request->completed)
                             {
                                 wait_result = Error(Anything, request_not_complete, BLOCK_DEVICE_REQUEST_PENDING);
                             }
                             else
                             {
                                 match_result(
                                     BlockRequestMap__delete(&registry.requests, request->request_id),
                                     success_do_nothing(),
                                     error(code, message)
                                     {
                                         __d_printf(IODEV, "block request delete failed id=%i code=%i\n",
                                                    (int)request->request_id, (int)code);
                                     }
                                 );
                                 completed_request = Some(UserspaceBlockRequestPtr, request);
                             }
                         },
                         none() { wait_result = Error(Anything, request_not_found, BLOCK_DEVICE_IO_ERROR); }
                     ););

    match_optional(
        completed_request,
        some(request)
        {
            if (request->status != SERVICE_OK)
            {
                UserspaceBlockRequest__release(request);
                return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
            }

            match_optional(
                request->continuation,
                some(continuation)
                {
                    match_optional(
                        request->io_context,
                        some(io_context)
                        {
                            io_context->buffer = ptr(request->buffer.kernel_addr.virtual_addr, Data);
                            io_context->buffer_size = request->block_count * BLOCK_DEVICE_BLOCK_SIZE;
                            io_context->completed_blocks = request->completed_blocks;
                            io_context->status = BLOCK_DEVICE_OK;
                            io_context->request_id = Some(BlockRequestID, request->request_id);
                            Result(Anything) continuation_result = continuation(io_context);
                            match_optional(
                                io_context->kernel_context.result_mapper,
                                some(mapper)
                                {
                                    Result(Anything) mapped_result =
                                        mapper(&continuation_result, io_context->kernel_context.result_mapper_context);
                                    UserspaceBlockRequest__release(request);
                                    return mapped_result;
                                },
                                none_do_nothing()
                            );
                            UserspaceBlockRequest__release(request);
                            return continuation_result;
                        },
                        none()
                        {
                            UserspaceBlockRequest__release(request);
                            return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
                        }
                    );
                },
                none_do_nothing()
            );

            Result(Anything) completed_blocks = Success(Anything, request->completed_blocks);
            match_optional(
                block_context->kernel_context.result_mapper,
                some(mapper)
                {
                    Result(Anything) mapped_result =
                        mapper(&completed_blocks, block_context->kernel_context.result_mapper_context);
                    UserspaceBlockRequest__release(request);
                    return mapped_result;
                },
                none_do_nothing()
            );

            UserspaceBlockRequest__release(request);
            return completed_blocks;
        },
        none_do_nothing()
    );

    return wait_result;
}

static Result(ServiceCode) UserspaceBlockDevice__park_for_request(UserspaceBlockRequestPtr request)
{
    Optional(KernelContextPtr) park_context = BlockDevice__current_context();
    match_optional(
        park_context,
        some_do_nothing(),
        none()
        {
            match_optional(
                request->io_context,
                some(io_context) { park_context = Some(KernelContextPtr, &io_context->kernel_context); },
                none_do_nothing()
            );
        }
    );

    match_optional(
        park_context,
        some(context)
        {
            match_optional(
                context->pid,
                some(pid)
                {
                    match_result(
                        BlockDeviceParkContext__heap_new(pid, &context->scheduler, request->request_id),
                        success(block_context)
                        {
                            critical_section(registry.lock, request->waiting_pid = Some(ProcessID, pid););

                            MessagePtr message = Message__heap_new_deferred_with_context_ownership_and_operation_key(
                                SOURCE_IO, SUBSYS_IO_PARK, pid, &block_context->kernel_context,
                                UserspaceBlockDevice__complete_wait, true, &Some(OperationKey, request->request_id));

                            match_result(
                                MessageBroker__deliver(request->device->message_broker, message),
                                success(_) { return Success(ServiceCode, SERVICE_OK); },
                                error(code, message)
                                {
                                    Kernel__free(addr(block_context));
                                    return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR);
                                }
                            );
                        },
                        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );
                },
                none() { return Success(ServiceCode, SERVICE_OK); }
            );
        },
        none() { return Success(ServiceCode, SERVICE_OK); }
    );

    return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR);
}

static Result(BlockCount)
    UserspaceBlockDevice__create_request(UserspaceBlockDevicePtr self, ServiceBlockOperation operation,
                                         BlockNumber block, BlockCount count, OptionalPtr(DataPtr) caller_buffer,
                                         OptionalPtr(BlockIOContextPtr) io_context,
                                         OptionalPtr(BlockIOContinuation) continuation)
{
    if (self == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (self->block_count != 0 && block + count > self->block_count)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    Optional(BlockIOContextPtr) io_context_value = *io_context;
    if (BlockIOContinuation__is_some(continuation) && is_nothing(io_context_value))
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    size_t buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    match_result(
        Process__alloc_user_pages(self->service_process, buffer_size),
        success(buffer)
        {
            if (operation == SERVICE_BLOCK_WRITE)
            {
                DataPtr kernel_buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
                Optional(DataPtr) caller_buffer_value = *caller_buffer;
                match_optional(
                    caller_buffer_value,
                    some(source)
                    {
                        for (size_t i = 0; i < buffer_size; i++)
                        {
                            kernel_buffer[i] = source[i];
                        }
                    },
                    none()
                    {
                        match_result(
                            Process__free_user_pages(self->service_process, buffer.virtual_addr),
                            success_do_nothing(),
                            error(free_code, free_message)
                            {
                                __d_printf(IODEV, "block write buffer cleanup failed code=%i\n", (int)free_code);
                            }
                        );
                        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
                    }
                );
            }

            match_result(
                UserspaceBlockRequest__heap_new(self, operation, block, count, buffer, buffer, caller_buffer,
                                                io_context, continuation),
                success(request)
                {
                    Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
                    critical_section(registry.lock, request->request_id = registry.next_request_id++;
                                     set_result =
                                         BlockRequestMap__set(&registry.requests, request->request_id, request););

                    match_result(
                        set_result,
                        success_do_nothing(),
                        error(code, message)
                        {
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                        }
                    );

                    __d_printf(IODEV,
                               "block request create id=%i op=%i block=%i count=%i continuation=%i "
                               "buffer_user=0x%x buffer_kernel=0x%x buffer_phys=0x%x size=%i\n",
                               (int)request->request_id, (int)request->operation, (int)request->block,
                               (int)request->block_count, (int)UserspaceBlockRequest__has_continuation(request),
                               (unsigned int)request->buffer.virtual_addr,
                               (unsigned int)request->buffer.kernel_addr.virtual_addr,
                               (unsigned int)request->buffer.kernel_addr.phys_addr, (int)buffer_size);

                    match_result(
                        UserspaceBlockDevice__deliver_service_event(self, self->service_process,
                                                                    UserspaceBlockRequest__service_event(request)),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );

                    match_result(
                        UserspaceBlockDevice__park_for_request(request),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );

                    return Error(BlockCount, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
                },
                error(code, message)
                {
                    match_result(
                        Process__free_user_pages(self->service_process, buffer.virtual_addr),
                        success_do_nothing(),
                        error(free_code, free_message)
                        {
                            __d_printf(IODEV, "block request buffer cleanup failed code=%i\n", (int)free_code);
                        }
                    );
                    return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                }
            );
        },
        error(code, message) { return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR); }
    );

    return Error(BlockCount, request_failed, BLOCK_DEVICE_IO_ERROR);
}

static Result(BlockCount)
    UserspaceBlockDevice__read(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr target)
{
    Optional(DataPtr) caller_buffer = Some(DataPtr, target);
    Optional(BlockIOContextPtr) io_context = None(BlockIOContextPtr);
    Optional(BlockIOContinuation) continuation = None(BlockIOContinuation);
    return UserspaceBlockDevice__create_request((UserspaceBlockDevicePtr)context, SERVICE_BLOCK_READ, block, count,
                                                &caller_buffer, &None(BlockIOContextPtr), &None(BlockIOContinuation));
}

static Result(BlockCount)
    UserspaceBlockDevice__write(BlockDriverContextPtr context, BlockNumber block, BlockCount count, DataPtr source)
{
    Optional(DataPtr) caller_buffer = Some(DataPtr, source);
    Optional(BlockIOContextPtr) io_context = None(BlockIOContextPtr);
    Optional(BlockIOContinuation) continuation = None(BlockIOContinuation);
    return UserspaceBlockDevice__create_request((UserspaceBlockDevicePtr)context, SERVICE_BLOCK_WRITE, block, count,
                                                &caller_buffer, &None(BlockIOContextPtr), &None(BlockIOContinuation));
}

BlockDevice BlockDevice__new(BlockDriverContextPtr context, BlockDeviceRead read, BlockDeviceWrite write)
{
    return (BlockDevice){
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

Result(Anything) BlockDevice__read_async(BlockDevicePtr self, BlockNumber block, BlockCount count,
                                         BlockIOContextPtr context, BlockIOContinuation continuation)
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
    match_optional(
        BlockDevice__current_context(),
        some(kernel_context) { context->kernel_context = *kernel_context; },
        none_do_nothing()
    );

    if (!self->userspace_service)
    {
        context->buffer = ptr(Kernel__alloc_kernel(context->buffer_size), Data);
        match_result(
            self->read(self->context, block, count, context->buffer),
            success(blocks)
            {
                context->completed_blocks = blocks;
                context->status = BLOCK_DEVICE_OK;
                return continuation(context);
            },
            error(code, message) { return Error(Anything, message, code); }
        );
    }

    match_result(
        Process__alloc_user_pages(((UserspaceBlockDevicePtr)self->context)->service_process, context->buffer_size),
        success(buffer)
        {
            UserspaceBlockDevicePtr userspace_device = (UserspaceBlockDevicePtr)self->context;
            Optional(DataPtr) caller_buffer = None(DataPtr);
            Optional(BlockIOContextPtr) io_context = Some(BlockIOContextPtr, context);
            Optional(BlockIOContinuation) block_continuation = Some(BlockIOContinuation, continuation);
            match_result(
                UserspaceBlockRequest__heap_new(userspace_device, SERVICE_BLOCK_READ, block, count, buffer, buffer,
                                                &caller_buffer, &io_context, &block_continuation),
                success(request)
                {
                    Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
                    critical_section(registry.lock, request->request_id = registry.next_request_id++;
                                     set_result =
                                         BlockRequestMap__set(&registry.requests, request->request_id, request););

                    match_result(
                        set_result,
                        success_do_nothing(),
                        error(code, message)
                        {
                            UserspaceBlockRequest__release(request);
                            return Error(Anything, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                        }
                    );

                    context->buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
                    context->request_id = Some(BlockRequestID, request->request_id);

                    __d_printf(IODEV,
                               "block async request create id=%i block=%i count=%i buffer_user=0x%x "
                               "buffer_kernel=0x%x buffer_phys=0x%x size=%i io_context=%p\n",
                               (int)request->request_id, (int)request->block, (int)request->block_count,
                               (unsigned int)request->buffer.virtual_addr,
                               (unsigned int)request->buffer.kernel_addr.virtual_addr,
                               (unsigned int)request->buffer.kernel_addr.phys_addr, (int)context->buffer_size, context);

                    match_result(
                        UserspaceBlockDevice__deliver_service_event(userspace_device, userspace_device->service_process,
                                                                    UserspaceBlockRequest__service_event(request)),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(Anything, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );

                    match_result(
                        UserspaceBlockDevice__park_for_request(request),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(Anything, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );
                    return Error(Anything, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
                },
                error(code, message)
                {
                    match_result(
                        Process__free_user_pages(((UserspaceBlockDevicePtr)self->context)->service_process,
                                                 buffer.virtual_addr),
                        success_do_nothing(),
                        error(free_code, free_message)
                        {
                            __d_printf(IODEV, "block async buffer cleanup failed code=%i\n", (int)free_code);
                        }
                    );
                    return Error(Anything, message, BLOCK_DEVICE_IO_ERROR);
                }
            );
        },
        error(code, message) { return Error(Anything, message, BLOCK_DEVICE_IO_ERROR); }
    );

    return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
}

Result(Anything) BlockDevice__write_async(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source,
                                          BlockIOContextPtr context, BlockIOContinuation continuation)
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
    match_optional(
        BlockDevice__current_context(),
        some(kernel_context) { context->kernel_context = *kernel_context; },
        none_do_nothing()
    );

    if (self->userspace_service)
    {
        Optional(DataPtr) caller_buffer = Some(DataPtr, source);
        Optional(BlockIOContextPtr) io_context = Some(BlockIOContextPtr, context);
        match_result(
            UserspaceBlockDevice__create_request((UserspaceBlockDevicePtr)self->context, SERVICE_BLOCK_WRITE, block,
                                                 count, &caller_buffer, &io_context,
                                                 &Some(BlockIOContinuation, continuation)),
            success(blocks) { return Success(Anything, blocks); },
            error(code, message) { return Error(Anything, message, code); }
        );
    }

    match_result(
        self->write(self->context, block, count, source),
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

Result(BlockDeviceCode) BlockDeviceRegistry__register(ConstBStringPtr path, ProcessPtr service_process,
                                                      BlockCount block_count, MessageBrokerPtr message_broker)
{
    if (BString__is_empty(path) || service_process == NULL || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    match_result(
        UserspaceBlockDevice__heap_new(&EMPTY_BSTRING, path, service_process, message_broker, block_count),
        success(device)
        {
            Result(BlockDeviceCode) register_result = Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
            critical_section(registry.lock,
                             match_optional(
                                 BlockDeviceRegistryMap__get(&registry.devices, *path),
                                 some(existing)
                                 {
                                     register_result =
                                         Error(BlockDeviceCode, already_registered, BLOCK_DEVICE_ALREADY_REGISTERED);
                                 },
                                 none()
                                 {
                                     device->alias = BString__sprintf(&hd_alias_format, (int)registry.next_device_id++);
                                     match_result(
                                         BlockDeviceRegistryMap__set(&registry.devices, *path, device),
                                         success(_) { register_result = Success(BlockDeviceCode, BLOCK_DEVICE_OK); },
                                         error(code, message)
                                         {
                                             register_result =
                                                 Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                                         }
                                     );
                                 }
                             ););

            match_result(
                register_result,
                success(code) { return Success(BlockDeviceCode, code); },
                error(code, message)
                {
                    Kernel__free(addr(device));
                    return Error(BlockDeviceCode, message, code);
                }
            );
        },
        error(code, message) { return Error(BlockDeviceCode, message, code); }
    );

    return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
}

static Result(BlockCount)
    UserspaceBlockDevice__create_shared_read_request(UserspaceBlockDevicePtr self, ProcessPtr requester,
                                                     BlockNumber block, BlockCount count, DataPtr requester_buffer,
                                                     size_t buffer_size)
{
    if (self == NULL || requester == NULL || requester_buffer == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    size_t required_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    if (buffer_size < required_size)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (self->block_count != 0 && block + count > self->block_count)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    match_result(
        Process__map_shared_user_pages_with_allocation(self->service_process, requester, addr(requester_buffer),
                                                       required_size),
        success(shared)
        {
            Optional(DataPtr) caller_buffer = None(DataPtr);
            match_result(
                UserspaceBlockRequest__heap_new(self, SERVICE_BLOCK_READ, block, count, shared.mapping,
                                                shared.allocation, &caller_buffer, &None(BlockIOContextPtr),
                                                &None(BlockIOContinuation)),
                success(request)
                {
                    Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
                    critical_section(registry.lock, request->request_id = registry.next_request_id++;
                                     set_result =
                                         BlockRequestMap__set(&registry.requests, request->request_id, request););

                    match_result(
                        set_result,
                        success_do_nothing(),
                        error(code, message)
                        {
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                        }
                    );

                    match_result(
                        UserspaceBlockDevice__deliver_service_event(self, self->service_process,
                                                                    UserspaceBlockRequest__service_event(request)),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );

                    match_result(
                        UserspaceBlockDevice__park_for_request(request),
                        success_do_nothing(),
                        error(code, message)
                        {
                            BlockDeviceRegistry__remove_request(request);
                            UserspaceBlockRequest__release(request);
                            return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                        }
                    );
                    return Error(BlockCount, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
                },
                error(code, message)
                {
                    match_result(
                        Process__free_user_pages(self->service_process, shared.allocation.virtual_addr),
                        success_do_nothing(),
                        error(free_code, free_message)
                        {
                            __d_printf(IODEV, "block shared buffer cleanup failed code=%i\n", (int)free_code);
                        }
                    );
                    return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR);
                }
            );
        },
        error(code, message) { return Error(BlockCount, message, BLOCK_DEVICE_INVALID_ARGUMENT); }
    );

    return Error(BlockCount, request_failed, BLOCK_DEVICE_IO_ERROR);
}

static Result(BlockRequestID) UserspaceBlockDevice__start_process_async_request(
    UserspaceBlockDevicePtr self, ProcessPtr requester, ServiceBlockOperation operation, BlockNumber block,
    BlockCount count, UserAddress buffer, UserAddress buffer_allocation, DataPtr requester_buffer, size_t required_size)
{
    if (operation == SERVICE_BLOCK_WRITE)
    {
        DataPtr kernel_buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
        for (size_t i = 0; i < required_size; i++)
        {
            kernel_buffer[i] = requester_buffer[i];
        }
    }

    Optional(DataPtr) caller_buffer = None(DataPtr);
    match_result(
        UserspaceBlockRequest__heap_new(self, operation, block, count, buffer, buffer_allocation, &caller_buffer,
                                        &None(BlockIOContextPtr), &None(BlockIOContinuation)),
        success(request)
        {
            Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
            critical_section(registry.lock, request->request_id = registry.next_request_id++;
                             request->response_process = Some(ProcessPtr, requester);
                             set_result = BlockRequestMap__set(&registry.requests, request->request_id, request););

            match_result(
                set_result,
                success_do_nothing(),
                error(code, message)
                {
                    UserspaceBlockRequest__release(request);
                    return Error(BlockRequestID, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                }
            );

            match_result(
                UserspaceBlockDevice__deliver_service_event(self, self->service_process,
                                                            UserspaceBlockRequest__service_event(request)),
                success(_) { return Success(BlockRequestID, request->request_id); },
                error(code, message)
                {
                    BlockDeviceRegistry__remove_request(request);
                    UserspaceBlockRequest__release(request);
                    return Error(BlockRequestID, message, BLOCK_DEVICE_IO_ERROR);
                }
            );
        },
        error(code, message)
        {
            match_result(
                Process__free_user_pages(self->service_process, buffer_allocation.virtual_addr),
                success_do_nothing(),
                error(free_code, free_message)
                {
                    __d_printf(IODEV, "block async process buffer cleanup failed code=%i\n", (int)free_code);
                }
            );
            return Error(BlockRequestID, message, BLOCK_DEVICE_IO_ERROR);
        }
    );

    return Error(BlockRequestID, request_failed, BLOCK_DEVICE_IO_ERROR);
}

static Result(BlockRequestID)
    UserspaceBlockDevice__create_process_async_request(UserspaceBlockDevicePtr self, ProcessPtr requester,
                                                       ServiceBlockOperation operation, BlockNumber block,
                                                       BlockCount count, DataPtr requester_buffer, size_t buffer_size)
{
    if (self == NULL || requester == NULL || requester_buffer == NULL || count == 0) // hundelint: ignore-no-null
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    size_t required_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    if (buffer_size < required_size)
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (self->block_count != 0 && block + count > self->block_count)
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (operation == SERVICE_BLOCK_READ)
    {
        match_result(
            Process__map_shared_user_pages_with_allocation(self->service_process, requester, addr(requester_buffer),
                                                           required_size),
            success(shared)
            {
                return UserspaceBlockDevice__start_process_async_request(self, requester, operation, block, count,
                                                                         shared.mapping, shared.allocation,
                                                                         requester_buffer, required_size);
            },
            error(code, message) { return Error(BlockRequestID, message, BLOCK_DEVICE_INVALID_ARGUMENT); }
        );
    }
    else if (operation == SERVICE_BLOCK_WRITE)
    {
        match_result(
            Process__alloc_user_pages(self->service_process, required_size),
            success(buffer)
            {
                return UserspaceBlockDevice__start_process_async_request(
                    self, requester, operation, block, count, buffer, buffer, requester_buffer, required_size);
            },
            error(code, message) { return Error(BlockRequestID, message, BLOCK_DEVICE_INVALID_ARGUMENT); }
        );
    }
    else
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return Error(BlockRequestID, request_failed, BLOCK_DEVICE_IO_ERROR);
}

Result(BlockCount) BlockDeviceRegistry__read_from_process(ProcessPtr requester, ConstBStringPtr path, BlockNumber block,
                                                          BlockCount count, DataPtr requester_buffer,
                                                          size_t buffer_size, MessageBrokerPtr message_broker)
{
    if (requester == NULL || BString__is_empty(path) || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    Optional(UserspaceBlockDevicePtr) device_result = None(UserspaceBlockDevicePtr);
    critical_section(registry.lock, device_result = BlockDeviceRegistryMap__get(&registry.devices, *path););

    match_optional(
        device_result,
        some(device)
        {
            if (device->message_broker != message_broker)
            {
                return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
            }

            return UserspaceBlockDevice__create_shared_read_request(device, requester, block, count, requester_buffer,
                                                                    buffer_size);
        },
        none_do_nothing()
    );

    return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
}

Result(BlockCount)
    BlockDeviceRegistry__write_from_process(ProcessPtr requester, ConstBStringPtr path, BlockNumber block,
                                            BlockCount count, DataPtr requester_buffer, size_t buffer_size,
                                            MessageBrokerPtr message_broker)
{
    if (requester == NULL || BString__is_empty(path) || requester_buffer == NULL || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    Optional(UserspaceBlockDevicePtr) device_result = None(UserspaceBlockDevicePtr);
    critical_section(registry.lock, device_result = BlockDeviceRegistryMap__get(&registry.devices, *path););

    match_optional(
        device_result,
        some(device)
        {
            if (device->message_broker != message_broker)
            {
                return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
            }

            if (buffer_size < count * BLOCK_DEVICE_BLOCK_SIZE)
            {
                return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
            }

            return BlockDevice__write(&device->block_device, block, count, requester_buffer);
        },
        none_do_nothing()
    );

    return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
}

Result(BlockRequestID)
    BlockDeviceRegistry__read_from_process_async(ProcessPtr requester, ConstBStringPtr path, BlockNumber block,
                                                 BlockCount count, DataPtr requester_buffer, size_t buffer_size,
                                                 MessageBrokerPtr message_broker)
{
    if (requester == NULL || BString__is_empty(path) || requester_buffer == NULL || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    Optional(UserspaceBlockDevicePtr) device_result = None(UserspaceBlockDevicePtr);
    critical_section(registry.lock, device_result = BlockDeviceRegistryMap__get(&registry.devices, *path););

    match_optional(
        device_result,
        some(device)
        {
            if (device->message_broker != message_broker)
            {
                return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
            }

            return UserspaceBlockDevice__create_process_async_request(device, requester, SERVICE_BLOCK_READ, block,
                                                                      count, requester_buffer, buffer_size);
        },
        none_do_nothing()
    );

    return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
}

Result(BlockRequestID)
    BlockDeviceRegistry__write_from_process_async(ProcessPtr requester, ConstBStringPtr path, BlockNumber block,
                                                  BlockCount count, DataPtr requester_buffer, size_t buffer_size,
                                                  MessageBrokerPtr message_broker)
{
    if (requester == NULL || BString__is_empty(path) || requester_buffer == NULL || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    Optional(UserspaceBlockDevicePtr) device_result = None(UserspaceBlockDevicePtr);
    critical_section(registry.lock, device_result = BlockDeviceRegistryMap__get(&registry.devices, *path););

    match_optional(
        device_result,
        some(device)
        {
            if (device->message_broker != message_broker)
            {
                return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
            }

            return UserspaceBlockDevice__create_process_async_request(device, requester, SERVICE_BLOCK_WRITE, block,
                                                                      count, requester_buffer, buffer_size);
        },
        none_do_nothing()
    );

    return Error(BlockRequestID, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
}

static Result(BlockChannelOpenMemory)
    BlockDeviceRegistry__open_channel_memory(ProcessPtr requester, ProcessPtr service_process, size_t size)
{
    BlockChannelID channel_id = 0;
    critical_section(registry.lock, channel_id = registry.next_channel_id++;);

    match_result(
        Process__alloc_user_pages(requester, size),
        success(requester_allocation)
        {
            Kernel__memset(requester_allocation.kernel_addr.virtual_addr, 0, size);
            match_result(
                Process__map_shared_user_pages_with_allocation(service_process, requester,
                                                               requester_allocation.virtual_addr, size),
                success(service_pages)
                {
                    BlockChannelOpenMemory opened = BlockChannelOpenMemory__new(
                        channel_id, &requester_allocation, &service_pages.allocation, &service_pages.mapping, size);
                    return Success(BlockChannelOpenMemory, opened);
                },
                error(code, message)
                {
                    match_result(
                        Process__free_user_pages(requester, requester_allocation.virtual_addr),
                        success_do_nothing(),
                        error(free_code, free_message)
                        {
                            __d_printf(IODEV, "block channel requester cleanup failed code=%i\n", (int)free_code);
                        }
                    );
                    return Error(BlockChannelOpenMemory, message, SERVICE_BLOCK_CHANNEL_FAILED);
                }
            );
        },
        error(code, message) { return Error(BlockChannelOpenMemory, message, SERVICE_BLOCK_CHANNEL_FAILED); }
    );

    return Error(BlockChannelOpenMemory, allocation_failed, SERVICE_BLOCK_CHANNEL_FAILED);
}

Result(ServiceCode) BlockDeviceRegistry__open_channel(ProcessPtr requester, ServiceBlockChannelOpenRequestPtr request,
                                                      MessageBrokerPtr message_broker)
{
    if (requester == NULL || request == NULL || BString__is_empty(&request->device_path) || message_broker == NULL) // hundelint: ignore-no-null
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    Optional(UserspaceBlockDevicePtr) device_result = None(UserspaceBlockDevicePtr);
    critical_section(registry.lock,
                     device_result = BlockDeviceRegistryMap__get(&registry.devices, request->device_path););

    match_optional(
        device_result,
        some(device)
        {
            if (device->message_broker != message_broker)
            {
                return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
            }

            match_result(
                BlockDeviceRegistry__open_channel_memory(requester, device->service_process,
                                                         sizeof(BlockChannelMemory)),
                success(opened)
                {
                    BlockChannelMemoryPtr channel_memory =
                        ptr(opened.requester_allocation.kernel_addr.virtual_addr, BlockChannelMemory);
                    BlockChannelMemory__init(channel_memory);

                    Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
                    match_result(
                        UserspaceBlockChannel__heap_new(opened.channel_id, device, requester,
                                                        &opened.requester_allocation, &opened.service_allocation),
                        success(channel)
                        {
                            critical_section(registry.lock, set_result = BlockChannelMap__set(
                                                                &registry.channels, opened.channel_id, channel););

                            match_result(
                                set_result,
                                success_do_nothing(),
                                error(code, message)
                                {
                                    BlockChannelState__ack_consumer_close(&channel_memory->state);
                                    UserspaceBlockChannel__release(channel, false);
                                    return Error(ServiceCode, registry_full, SERVICE_BLOCK_CHANNEL_FAILED);
                                }
                            );

                            request->channel_id = opened.channel_id;
                            request->channel = opened.requester_allocation.virtual_addr;
                            request->channel_size = opened.size;

                            match_result(
                                UserspaceBlockDevice__deliver_service_event(
                                    device, device->service_process,
                                    ServiceEvent__new(SERVICE_EVENT_BLOCK_CHANNEL_OPEN, opened.channel_id,
                                                      opened.service_mapping.virtual_addr)),
                                success(code) { return Success(ServiceCode, code); },
                                error(code, message)
                                {
                                    critical_section(registry.lock,
                                                     BlockChannelMap__delete(&registry.channels, opened.channel_id););
                                    BlockChannelState__ack_consumer_close(&channel_memory->state);
                                    UserspaceBlockChannel__release(channel, false);
                                    return Error(ServiceCode, message, SERVICE_BLOCK_CHANNEL_FAILED);
                                }
                            );
                        },
                        error(code, message)
                        {
                            match_result(
                                Process__free_user_pages(device->service_process,
                                                         opened.service_allocation.virtual_addr),
                                success_do_nothing(),
                                error(free_code, free_message)
                                {
                                    __d_printf(IODEV, "block channel service cleanup failed code=%i\n", (int)free_code);
                                }
                            );
                            match_result(
                                Process__free_user_pages(requester, opened.requester_allocation.virtual_addr),
                                success_do_nothing(),
                                error(free_code, free_message)
                                {
                                    __d_printf(IODEV, "block channel requester cleanup failed code=%i\n",
                                               (int)free_code);
                                }
                            );
                            return Error(ServiceCode, message, SERVICE_BLOCK_CHANNEL_FAILED);
                        }
                    );
                },
                error(code, message) { return Error(ServiceCode, message, SERVICE_BLOCK_CHANNEL_FAILED); }
            );
        },
        none_do_nothing()
    );

    return Error(ServiceCode, invalid_argument, SERVICE_BLOCK_CHANNEL_NOT_FOUND);
}

Result(ServiceCode) BlockDeviceRegistry__close_channel(ProcessPtr requester, ServiceBlockChannelCloseRequestPtr request)
{
    if (requester == NULL || request == NULL || request->channel_id == 0) // hundelint: ignore-no-null
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    Optional(UserspaceBlockChannelPtr) channel_result = None(UserspaceBlockChannelPtr);
    critical_section(registry.lock, match_optional(
                                        BlockChannelMap__get(&registry.channels, request->channel_id),
                                        some(channel)
        {
                                            if (channel->requester == requester)
                                            {
                                                channel_result = Some(UserspaceBlockChannelPtr, channel);
                                                BlockChannelMap__delete(&registry.channels, request->channel_id);
                                            }
                                        },
                                        none_do_nothing()
    ););

    match_optional(
        channel_result,
        some(channel) { return UserspaceBlockChannel__release(channel, true); },
        none_do_nothing()
    );

    return Error(ServiceCode, request_not_found, SERVICE_BLOCK_CHANNEL_NOT_FOUND);
}

Result(BlockDeviceCode) BlockDeviceRegistry__dispose_process_resources(ProcessPtr process)
{
    if (process == NULL) // hundelint: ignore-no-null
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockRequestIDs stale_request_ids = BlockRequestIDs__new();
    BlockRequests stale_requests = BlockRequests__new();
    BlockChannels stale_channels = BlockChannels__new();
    UserspaceBlockDevices stale_devices = UserspaceBlockDevices__new();
    BlockDevicePaths stale_paths = EmptyArray(BlockDevicePaths);

    critical_section(
        registry.lock, for_each_map(BlockRequestMap, registry.requests, index, request_id, request,
                                    {
                                        if (request->device->service_process == process)
                                        {
                                            match_result(
                                                BlockRequestIDs__push(&stale_request_ids, request_id),
                                                success_do_nothing(),
                                                error(code, message)
                                                {
                                                    cs_return Error(BlockDeviceCode, registry_full,
                                                                    BLOCK_DEVICE_REGISTRY_FULL);
                                                }
                                            );
                                        }
                                    });
        for_each_map(BlockChannelMap, registry.channels, channel_index, channel_id, channel,
                     {
                         if (channel->requester == process || channel->device->service_process == process)
                         {
                             match_result(
                                 BlockChannels__push(&stale_channels, channel),
                                 success_do_nothing(),
                                 error(code, message)
                                 {
                                     cs_return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                                 }
                             );
                         }
                     });
        for_each_fa(BlockChannels, stale_channels, channel_index, stale_channel,
                    {
                        match_result(
                            BlockChannelMap__delete(&registry.channels, stale_channel->channel_id),
                            success_do_nothing(),
                            error(code, message)
                            {
                                __d_printf(IODEV, "block channel stale delete failed id=%i code=%i\n",
                                           (int)stale_channel->channel_id, (int)code);
                            }
                        );
                    });
        for_each_map(BlockDeviceRegistryMap, registry.devices, index, path, device,
                     {
                         if (device->service_process == process)
                         {
                             match_result(
                                 BlockDevicePaths__push(&stale_paths, path),
                                 success_do_nothing(),
                                 error(code, message)
                                 {
                                     cs_return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                                 }
                             );
                         }
                     });
        for_each_fa(BlockDevicePaths, stale_paths, path_index, stale_path,
                    {
                        match_optional(
                            BlockDeviceRegistryMap__get(&registry.devices, stale_path),
                            some(device)
                            {
                                BlockDeviceRegistryMap__delete(&registry.devices, stale_path);
                                match_result(
                                    UserspaceBlockDevices__push(&stale_devices, device),
                                    success_do_nothing(),
                                    error(code, message)
                                    {
                                        cs_return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                                    }
                                );
                            },
                            none_do_nothing()
                        );
                    });
        for_each_fa(BlockRequestIDs, stale_request_ids, request_index, stale_request_id, {
            match_optional(
                BlockRequestMap__get(&registry.requests, stale_request_id),
                some(request)
                {
                    match_result(
                        BlockRequests__push(&stale_requests, request),
                        success_do_nothing(),
                        error(code, message)
                        {
                            cs_return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
                        }
                    );
                    match_result(
                        BlockRequestMap__delete(&registry.requests, stale_request_id),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __d_printf(IODEV, "block request stale delete failed id=%i code=%i\n",
                                       (int)stale_request_id, (int)code);
                        }
                    );
                },
                none_do_nothing()
            );
        }););

    for_each_fa(BlockRequests, stale_requests, request_index, request, { UserspaceBlockRequest__release(request); });

    for_each_fa(BlockChannels, stale_channels, channel_index, channel,
                { UserspaceBlockChannel__release(channel, channel->device->service_process != process); });

    for_each_fa(UserspaceBlockDevices, stale_devices, device_index, device, { Kernel__free(addr(device)); });

    return Success(BlockDeviceCode, BLOCK_DEVICE_OK);
}

Result(ServiceBlockRequest) BlockDeviceRegistry__get_request(ProcessPtr service_process, uint64_t request_id)
{
    Result(ServiceBlockRequest) request_result =
        Error(ServiceBlockRequest, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);

    critical_section(
        registry.lock,
        match_optional(
            BlockRequestMap__get(&registry.requests, request_id),
            some(request)
            {
                if (request->device->service_process != service_process)
                {
                    request_result = Error(ServiceBlockRequest, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
                }
                else
                {
                    request_result = Success(ServiceBlockRequest, UserspaceBlockRequest__service_request(request));
                }
            },
            none()
            {
                request_result = Error(ServiceBlockRequest, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
            }
        ););

    return request_result;
}

Result(ServiceCode) BlockDeviceService__complete(ProcessPtr service_process, ServiceBlockCompletionPtr completion)
{
    Optional(ProcessID) waiting_pid = None(ProcessID);
    Optional(MessageBrokerPtr) message_broker = None(MessageBrokerPtr);
    Optional(ProcessPtr) response_process = None(ProcessPtr);
    Optional(UserspaceBlockRequestPtr) response_request = None(UserspaceBlockRequestPtr);
    Result(ServiceCode) completion_result = Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);

    critical_section(
        registry.lock,
        match_optional(
            BlockRequestMap__get(&registry.requests, completion->request_id),
            some(request)
            {
                if (request->device->service_process != service_process)
                {
                    completion_result = Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
                }
                else if (completion->block_count > request->block_count)
                {
                    completion_result = Error(ServiceCode, request_failed, SERVICE_BLOCK_REQUEST_FAILED);
                }
                else
                {
                    if (completion->status == SERVICE_OK && request->operation == SERVICE_BLOCK_READ)
                    {
                        match_optional(
                            request->caller_buffer,
                            some(caller_buffer)
                            {
                                size_t byte_count = completion->block_count * BLOCK_DEVICE_BLOCK_SIZE;
                                DataPtr kernel_buffer = ptr(request->buffer.kernel_addr.virtual_addr, Data);
                                for (size_t i = 0; i < byte_count; i++)
                                {
                                    caller_buffer[i] = kernel_buffer[i];
                                }
                            },
                            none_do_nothing()
                        );
                    }

                    request->completed = true;
                    request->status = completion->status;
                    request->completed_blocks = completion->block_count;
                    waiting_pid = request->waiting_pid;
                    response_process = request->response_process;
                    response_request = Some(UserspaceBlockRequestPtr, request);
                    message_broker = Some(MessageBrokerPtr, request->device->message_broker);
                    completion_result = Success(ServiceCode, SERVICE_OK);
                }
            },
            none() { completion_result = Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND); }
        ););

    match_result(
        completion_result,
        success_do_nothing(),
        error(code, message) { return Error(ServiceCode, message, code); }
    );

    match_optional(
        waiting_pid,
        some(pid)
        {
            match_optional(
                message_broker,
                some(broker)
                {
                    MessagePtr message = Message__heap_new_io_operation(SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid,
                                                                        completion->request_id);
                    match_result(
                        MessageBroker__deliver(broker, message),
                        success_do_nothing(),
                        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );
                },
                none() { return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR); }
            );
        },
        none_do_nothing()
    );

    match_optional(
        response_process,
        some(process)
        {
            match_optional(
                response_request,
                some(request)
                {
                    match_result(
                        UserspaceBlockDevice__deliver_service_event(
                            request->device, process,
                            ServiceEvent__response(SERVICE_EVENT_BLOCK_IO, completion->request_id,
                                                   completion->status == SERVICE_OK ? completion->block_count : 0)),
                        success_do_nothing(),
                        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );

                    BlockDeviceRegistry__remove_request(request);
                    UserspaceBlockRequest__release(request);
                },
                none() { return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR); }
            );
        },
        none_do_nothing()
    );

    return Success(ServiceCode, SERVICE_OK);
}

Optional(BlockDevicePtr) BlockDeviceRegistry__find(ConstBStringPtr path)
{
    Optional(BlockDevicePtr) device_result = None(BlockDevicePtr);

    critical_section(registry.lock, match_optional(
                                        BlockDeviceRegistryMap__get(&registry.devices, *path),
                                        some(device) { device_result = Some(BlockDevicePtr, &device->block_device); },
                                        none_do_nothing()
    ););

    return device_result;
}

BlockDevicePaths BlockDeviceRegistry__paths()
{
    BlockDevicePaths paths = EmptyArray(BlockDevicePaths);
    critical_section(registry.lock, for_each_map(BlockDeviceRegistryMap, registry.devices, index, path, device,
                                                 { BlockDevicePaths__push(&paths, path); }););
    return paths;
}

BlockDeviceMetadataEntries BlockDeviceRegistry__metadata_entries()
{
    BlockDeviceMetadataEntries entries = EmptyArray(BlockDeviceMetadataEntries);
    critical_section(registry.lock, for_each_map(BlockDeviceRegistryMap, registry.devices, index, path, device, {
                         BlockDeviceMetadataEntries__push(&entries, BlockDeviceMetadata__from_device(device));
                     }););
    return entries;
}

Optional(BlockDeviceMetadata) BlockDeviceRegistry__metadata(ConstBStringPtr alias)
{
    Optional(BlockDeviceMetadata) metadata = None(BlockDeviceMetadata);

    critical_section(registry.lock, for_each_map(BlockDeviceRegistryMap, registry.devices, index, path, device, {
                         if (BString__equals(&device->alias, alias))
                         {
                             metadata = Some(BlockDeviceMetadata, BlockDeviceMetadata__from_device(device));
                         }
                     }););
    return metadata;
}

#include "block_device.h"

#include "../../../all_defs.h"
#include "../../../kalloc.h"
#include "../../../process.h"
#include "../../../service/service.h"
#include "../../../scheduler/scheduler.h"
#include "../../../scheduler/parked.h"
#include "../../../types/map.h"

static KString invalid_argument = KSTRING_NEW("Invalid block device argument");
static KString already_registered = KSTRING_NEW("Block device already registered");
static KString registry_full = KSTRING_NEW("Block device registry full");
static KString request_pending = KSTRING_NEW("Userspace block IO request pending");
static KString request_not_found = KSTRING_NEW("Userspace block IO request not found");
static KString request_failed = KSTRING_NEW("Userspace block IO request failed");
static KString request_not_complete = KSTRING_NEW("Userspace block IO request is not complete");

#define MAX_BLOCK_REQUESTS IO_MAX_DEVICES

Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size) __attribute__((weak));
Result(UserAddress) Process__alloc_user_pages(ProcessPtr self, size_t size)
{
    return Error(UserAddress, invalid_argument, BLOCK_DEVICE_IO_ERROR);
}

typedef struct UserspaceBlockDevice
{
    KString path;
    ProcessPtr service_process;
    SchedulerPtr scheduler;
    BlockCount block_count;
    BlockDevice block_device;
}
UserspaceBlockDevice;

typedef UserspaceBlockDevice * UserspaceBlockDevicePtr;

OptionalDef(UserspaceBlockDevicePtr);

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
MapDefine(KString, UserspaceBlockDevicePtr, IO_MAX_DEVICES, BlockDeviceRegistryMap, kstring_equals);
MapBodyDefine(BlockDeviceRegistryMap);
MapDefine(BlockRequestID, UserspaceBlockRequestPtr, MAX_BLOCK_REQUESTS, BlockRequestMap, SimpleEquals(uint64_t));
MapBodyDefine(BlockRequestMap);

static BlockDeviceRegistryMap registry = EmptyMap(BlockDeviceRegistryMap);
static BlockRequestMap requests = EmptyMap(BlockRequestMap);
static bool registry_initialized = false;
static uint64_t next_request_id = 1;
static Optional(KernelContextPtr) current_context = None(KernelContextPtr);

static void BlockDeviceRegistry__init()
{
    if (registry_initialized)
    {
        return;
    }

    registry = BlockDeviceRegistryMap__new();
    requests = BlockRequestMap__new();
    registry_initialized = true;
}

static Result(Anything) UserspaceBlockDevice__complete_wait(ParkedKernelContextPtr parked_context)
{
    BlockDeviceParkContextPtr block_context = (BlockDeviceParkContextPtr) parked_context->kernel_context;

    Match(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, block_context->request_id))
        IsSome(request,
        {
            if (!request->completed)
            {
                return Error(Anything, request_not_complete, BLOCK_DEVICE_REQUEST_PENDING);
            }

            if (request->status != SERVICE_OK)
            {
                return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
            }

            Match(BlockIOContinuation, request->continuation)
                IsSome(continuation,
                {
                    request->io_context->buffer = ptr(request->buffer.kernel_addr.virtual_addr, Data);
                    request->io_context->buffer_size = request->block_count * BLOCK_DEVICE_BLOCK_SIZE;
                    request->io_context->completed_blocks = request->completed_blocks;
                    request->io_context->status = BLOCK_DEVICE_OK;
                    request->io_context->request_id = Some(BlockRequestID, request->request_id);
                    Result(Anything) continuation_result = continuation(request->io_context);
                    Match(KernelResultMapper, request->io_context->kernel_context.result_mapper)
                        IsSome(mapper, return mapper(
                            continuation_result,
                            request->io_context->kernel_context.result_mapper_context
                        ))
                    End
                    return continuation_result;
                })
            End

            Result(Anything) completed_blocks = Success(Anything, request->completed_blocks);
            Match(KernelResultMapper, block_context->kernel_context.result_mapper)
                IsSome(mapper, return mapper(completed_blocks, block_context->kernel_context.result_mapper_context))
            End

            return completed_blocks;
        })
    End

    return Error(Anything, request_not_found, BLOCK_DEVICE_IO_ERROR);
}

static void UserspaceBlockDevice__park_for_request(UserspaceBlockRequestPtr request)
{
    Optional(KernelContextPtr) park_context = current_context;
    if (!park_context.present && request->io_context != 0)
    {
        park_context = Some(KernelContextPtr, &request->io_context->kernel_context);
    }

    Match(KernelContextPtr, park_context)
        IsSome(context,
        {
            Match(ProcessID, context->pid)
                IsSome(pid,
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

                    ResultMatch(ProcessID, Scheduler__park_by_pid(
                        request->device->scheduler,
                        pid,
                        (KernelContextPtr) block_context,
                        Some(Continuation, UserspaceBlockDevice__complete_wait),
                        PARK_WAIT_IO
                    ))
                        IsError(code, message, {})
                    End
                })
            End
        })
    End
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
    if (self == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (caller_buffer == NULL && !continuation.present)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    if (self->block_count != 0 && block + count > self->block_count)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    size_t buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    ResultMatch(UserAddress, Process__alloc_user_pages(self->service_process, buffer_size))
        IsSuccess(buffer,
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

            ResultMatch(CollectionCode, BlockRequestMap__set(&requests, request->request_id, request))
                IsError(code, message, return Error(BlockCount, registry_full, BLOCK_DEVICE_REGISTRY_FULL))
            End

            __d_printf(
                IODEV,
                "block request create id=%i op=%i block=%i count=%i io_context=%p continuation=%i\n",
                (int) request->request_id,
                (int) request->operation,
                (int) request->block,
                (int) request->block_count,
                request->io_context,
                (int) request->continuation.present
            );

            ServiceEvent event;
            event.type = SERVICE_EVENT_BLOCK_IO;
            event.source = request->request_id;
            event.data = 0;

            ResultMatch(ServiceCode, Scheduler__service_enqueue(self->scheduler, self->service_process, event))
                IsError(code, message, return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR))
            End

            UserspaceBlockDevice__park_for_request(request);

            return Error(BlockCount, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
        })
        IsError(code, message, return Error(BlockCount, message, BLOCK_DEVICE_IO_ERROR))
    End

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
        NULL,
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
        NULL,
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
    if (self == NULL || self->read == NULL || target == NULL || count == 0)
    {
        return Error(BlockCount, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    return self->read(self->context, block, count, target);
}

Result(BlockCount) BlockDevice__write(BlockDevicePtr self, BlockNumber block, BlockCount count, DataPtr source)
{
    if (self == NULL || self->write == NULL || source == NULL || count == 0)
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
    if (self == NULL || context == NULL || continuation == NULL || count == 0)
    {
        return Error(Anything, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    context->block = block;
    context->block_count = count;
    context->completed_blocks = 0;
    context->status = BLOCK_DEVICE_REQUEST_PENDING;
    context->request_id = None(BlockRequestID);
    context->buffer_size = count * BLOCK_DEVICE_BLOCK_SIZE;
    Match(KernelContextPtr, current_context)
        IsSome(kernel_context, context->kernel_context = *kernel_context)
    End

    if (!self->userspace_service)
    {
        context->buffer = ptr(Kernel__alloc_kernel(context->buffer_size), Data);
        ResultMatch(BlockCount, self->read(self->context, block, count, context->buffer))
            IsSuccess(blocks,
            {
                context->completed_blocks = blocks;
                context->status = BLOCK_DEVICE_OK;
                return continuation(context);
            })
            IsError(code, message, return Error(Anything, message, code))
        End
    }

    ResultMatch(UserAddress, Process__alloc_user_pages(((UserspaceBlockDevicePtr) self->context)->service_process, context->buffer_size))
        IsSuccess(buffer,
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
            request->caller_buffer = NULL;
            request->io_context = context;
            request->continuation = Some(BlockIOContinuation, continuation);
            request->completed = false;
            request->status = SERVICE_EVENT_QUEUE_EMPTY;
            request->completed_blocks = 0;
            request->waiting_pid = None(ProcessID);

            context->buffer = ptr(buffer.kernel_addr.virtual_addr, Data);
            context->request_id = Some(BlockRequestID, request->request_id);

            ResultMatch(CollectionCode, BlockRequestMap__set(&requests, request->request_id, request))
                IsError(code, message, return Error(Anything, registry_full, BLOCK_DEVICE_REGISTRY_FULL))
            End

            ServiceEvent event;
            event.type = SERVICE_EVENT_BLOCK_IO;
            event.source = request->request_id;
            event.data = 0;

            ResultMatch(ServiceCode, Scheduler__service_enqueue(
                userspace_device->scheduler, userspace_device->service_process, event
            ))
                IsError(code, message, return Error(Anything, message, BLOCK_DEVICE_IO_ERROR))
            End

            UserspaceBlockDevice__park_for_request(request);
            return Error(Anything, request_pending, BLOCK_DEVICE_REQUEST_PENDING);
        })
        IsError(code, message, return Error(Anything, message, BLOCK_DEVICE_IO_ERROR))
    End

    return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
}

Result(Anything) BlockDevice__write_async(
    BlockDevicePtr self, BlockNumber block, BlockCount count,
    DataPtr source, BlockIOContextPtr context, BlockIOContinuation continuation
)
{
    if (self == NULL || source == NULL || context == NULL || continuation == NULL || count == 0)
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
    Match(KernelContextPtr, current_context)
        IsSome(kernel_context, context->kernel_context = *kernel_context)
    End

    if (self->userspace_service)
    {
        ResultMatch(BlockCount, UserspaceBlockDevice__create_request(
            (UserspaceBlockDevicePtr) self->context,
            SERVICE_BLOCK_WRITE,
            block,
            count,
            source,
            context,
            Some(BlockIOContinuation, continuation)
        ))
            IsSuccess(blocks, return Success(Anything, blocks))
            IsError(code, message, return Error(Anything, message, code))
        End
    }

    ResultMatch(BlockCount, self->write(self->context, block, count, source))
        IsSuccess(blocks,
        {
            context->completed_blocks = blocks;
            context->status = blocks == count ? BLOCK_DEVICE_OK : BLOCK_DEVICE_IO_ERROR;
            return continuation(context);
        })
        IsError(code, message, return Error(Anything, message, code))
    End

    return Error(Anything, request_failed, BLOCK_DEVICE_IO_ERROR);
}

void BlockDevice__set_current_context(KernelContextPtr context)
{
    current_context = Some(KernelContextPtr, context);
}

void BlockDevice__clear_current_context()
{
    current_context = None(KernelContextPtr);
}

Result(BlockDeviceCode) BlockDeviceRegistry__register(
    KString path, ProcessPtr service_process, BlockCount block_count, SchedulerPtr scheduler
)
{
    if (path.len == 0 || service_process == NULL || scheduler == NULL)
    {
        return Error(BlockDeviceCode, invalid_argument, BLOCK_DEVICE_INVALID_ARGUMENT);
    }

    BlockDeviceRegistry__init();

    Match(UserspaceBlockDevicePtr, BlockDeviceRegistryMap__get(&registry, path))
        IsSome(existing, return Error(BlockDeviceCode, already_registered, BLOCK_DEVICE_ALREADY_REGISTERED))
    End

    UserspaceBlockDevicePtr device = ptr(Kernel__alloc_kernel(sizeof(UserspaceBlockDevice)), UserspaceBlockDevice);
    *device = (UserspaceBlockDevice) {
        .path = path,
        .service_process = service_process,
        .scheduler = scheduler,
        .block_count = block_count,
    };
    device->block_device = BlockDevice__new(device, UserspaceBlockDevice__read, UserspaceBlockDevice__write);
    device->block_device.userspace_service = true;

    ResultMatch(CollectionCode, BlockDeviceRegistryMap__set(&registry, path, device))
        IsSuccess(_, return Success(BlockDeviceCode, BLOCK_DEVICE_OK))
        IsError(code, message, return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL))
    End

    return Error(BlockDeviceCode, registry_full, BLOCK_DEVICE_REGISTRY_FULL);
}

Result(ServiceCode) BlockDeviceService__get_request(
    ProcessPtr service_process, uint64_t request_id, ServiceBlockRequestPtr target
)
{
    if (service_process == NULL || target == NULL)
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    BlockDeviceRegistry__init();

    Match(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, request_id))
        IsSome(request,
        {
            if (request->device->service_process != service_process)
            {
                return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
            }

            target->request_id = request->request_id;
            target->operation = request->operation;
            target->block = request->block;
            target->block_count = request->block_count;
            target->block_size = BLOCK_DEVICE_BLOCK_SIZE;
            target->buffer = ptr(request->buffer.virtual_addr, void);
            target->buffer_size = request->block_count * BLOCK_DEVICE_BLOCK_SIZE;
            return Success(ServiceCode, SERVICE_OK);
        })
    End

    return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
}

Result(ServiceCode) BlockDeviceService__complete(
    ProcessPtr service_process, ServiceBlockCompletionPtr completion
)
{
    if (service_process == NULL || completion == NULL)
    {
        return Error(ServiceCode, invalid_argument, SERVICE_INVALID_BUFFER);
    }

    BlockDeviceRegistry__init();

    Match(UserspaceBlockRequestPtr, BlockRequestMap__get(&requests, completion->request_id))
        IsSome(request,
        {
            if (request->device->service_process != service_process)
            {
                return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
            }

            if (completion->block_count > request->block_count)
            {
                return Error(ServiceCode, request_failed, SERVICE_BLOCK_REQUEST_FAILED);
            }

            if (completion->status == SERVICE_OK && request->operation == SERVICE_BLOCK_READ && request->caller_buffer != NULL)
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

            Match(ProcessID, request->waiting_pid)
                IsSome(pid,
                {
                    ResultMatch(ProcessID, Scheduler__unpark_by_pid(
                        request->device->scheduler, pid, PARK_WAIT_IO, None(ReturnCode)
                    ))
                        IsError(code, message, {})
                    End
                })
            End

            return Success(ServiceCode, SERVICE_OK);
        })
    End

    return Error(ServiceCode, request_not_found, SERVICE_BLOCK_REQUEST_NOT_FOUND);
}

Optional(BlockDevicePtr) BlockDeviceRegistry__find(KString path)
{
    BlockDeviceRegistry__init();

    Match(UserspaceBlockDevicePtr, BlockDeviceRegistryMap__get(&registry, path))
        IsSome(device, return Some(BlockDevicePtr, &device->block_device))
    End

    return None(BlockDevicePtr);
}

BlockDevicePaths BlockDeviceRegistry__paths()
{
    BlockDeviceRegistry__init();

    BlockDevicePaths paths = EmptyArray(BlockDevicePaths);
    MapIterate(BlockDeviceRegistryMap, registry, index, path, device)
        BlockDevicePaths__push(&paths, path);
    End

    return paths;
}

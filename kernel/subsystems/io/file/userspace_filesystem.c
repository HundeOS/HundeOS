#include "userspace_filesystem.h"

#include "../../../kalloc.h"
#include "../../../mutex.h"
#include "../../../scheduler/scheduler.h"
#include "../../../scheduler/parked.h"
#include "../../../subsystems/io/block/block_device.h"
#include "../../../subsystems/message/io_operation_message.h"
#include "../../../subsystems/message/subsystem_codes.h"
#include "../../../types/map.h"

static BString invalid_argument = bstr("Invalid userspace filesystem argument");
static BString registry_full = bstr("Userspace filesystem registry full");
static BString request_pending = bstr("Userspace filesystem request pending");
static BString request_not_found = bstr("Userspace filesystem request not found");
static BString request_failed = bstr("Userspace filesystem request failed");
static BString request_not_complete = bstr("Userspace filesystem request is not complete");
static BString allocation_failed = bstr("Userspace filesystem allocation failed");

#define MAX_USERSPACE_FILESYSTEM_REQUESTS IO_MAX_DEVICES

typedef struct UserspaceFilesystem
{
    BString device_path;
    BString filesystem_type;
    BString mountpoint;
    ProcessPtr service_process;
    MessageBrokerPtr message_broker;
    FilesystemPtr filesystem;
}
UserspaceFilesystem;

typedef UserspaceFilesystem * UserspaceFilesystemPtr;

typedef struct UserspaceFilesystemRequestState
{
    uint64_t request_id;
    UserspaceFilesystemPtr filesystem;
    ServiceFilesystemOperation operation;
    BString path;
    IOInternalHandle handle;
    Flags flags;
    uint64_t block;
    uint64_t block_offset;
    UserAddress buffer;
    size_t size;
    UserAddress entries;
    size_t max_entries;
    UserAddress shared_buffer_allocation;
    bool has_shared_buffer;
    OperationKey wake_key;
    bool completed;
    ServiceCode status;
    int64_t result;
    FileMode mode;
    uint64_t file_size;
    uint32_t blocks[12];
    Optional(ProcessID) waiting_pid;
}
UserspaceFilesystemRequestState;

typedef UserspaceFilesystemRequestState * UserspaceFilesystemRequestStatePtr;

typedef struct UserspaceFilesystemFileHandle
{
    BString path;
    IOInternalHandle handle;
    Flags flags;
    FileMode mode;
    uint64_t size;
    uint64_t offset;
    uint32_t block[12];
}
UserspaceFilesystemFileHandle;

typedef UserspaceFilesystemFileHandle * UserspaceFilesystemFileHandlePtr;

OptionalDef(UserspaceFilesystemFileHandle);
ResultDef(UserspaceFilesystemFileHandlePtr);

MapDefine(
    IOInternalHandle,
    UserspaceFilesystemFileHandle,
    MAX_HANDLES_PER_DEVICE,
    UserspaceFilesystemHandleMap,
    SimpleEquals(uint32_t)
);
MapBodyDefine(UserspaceFilesystemHandleMap);

typedef struct UserspaceFilesystemIOState
{
    FilesystemIOState state;
    UserspaceFilesystemPtr filesystem;
    UserspaceFilesystemHandleMap handles;
    uint32_t next_handle;
}
UserspaceFilesystemIOState;

typedef UserspaceFilesystemIOState * UserspaceFilesystemIOStatePtr;

typedef struct UserspaceFilesystemParkContext
{
    KernelContext kernel_context;
    uint64_t request_id;
}
UserspaceFilesystemParkContext;

typedef UserspaceFilesystemParkContext * UserspaceFilesystemParkContextPtr;

typedef struct UserspaceFilesystemRequestStart
{
    UserspaceFilesystemRequestStatePtr request;
    UserspaceFilesystemParkContextPtr park_context;
    ServiceEvent event;
}
UserspaceFilesystemRequestStart;

typedef const UserspaceFilesystemRequestStart * ConstUserspaceFilesystemRequestStartPtr;

OptionalDef(UserspaceFilesystemPtr);
OptionalDef(UserspaceFilesystemRequestStatePtr);
OptionalDef(MessageBrokerPtr);
ResultDef(UserspaceFilesystemPtr);
ResultDef(UserspaceFilesystemIOStatePtr);
ResultDef(UserspaceFilesystemRequestStatePtr);
ResultDef(UserspaceFilesystemParkContextPtr);
ResultDef(UserspaceFilesystemRequestStart);
MapDefine(BString, UserspaceFilesystemPtr, IO_MAX_DEVICES, UserspaceFilesystemRegistryMap, bstring_equals);
MapBodyDefine(UserspaceFilesystemRegistryMap);
MapDefine(
    uint64_t,
    UserspaceFilesystemRequestStatePtr,
    MAX_USERSPACE_FILESYSTEM_REQUESTS,
    UserspaceFilesystemRequestMap,
    SimpleEquals(uint64_t)
);
MapBodyDefine(UserspaceFilesystemRequestMap);

struct UserspaceFilesystemRegistry
{
    mutex_t lock;
    UserspaceFilesystemRegistryMap filesystems;
    UserspaceFilesystemRequestMap requests;
    uint64_t next_request_id;
};

#define USERSPACE_FILESYSTEM_REGISTRY_INIT \
    (UserspaceFilesystemRegistry) { \
        .lock = MUTEX_INIT, \
        .filesystems = EmptyMap(UserspaceFilesystemRegistryMap), \
        .requests = EmptyMap(UserspaceFilesystemRequestMap), \
        .next_request_id = 1, \
    }

static UserspaceFilesystemRegistry shared_registry = USERSPACE_FILESYSTEM_REGISTRY_INIT;

UserspaceFilesystemRegistryPtr UserspaceFilesystemRegistry__shared(void)
{
    return &shared_registry;
}

static UserspaceFilesystem UserspaceFilesystem__new(
    ConstBStringPtr device_path,
    ConstBStringPtr filesystem_type,
    ConstBStringPtr mountpoint,
    ProcessPtr service_process,
    MessageBrokerPtr message_broker,
    FilesystemPtr filesystem
)
{
    return (UserspaceFilesystem) {
        .device_path = *device_path,
        .filesystem_type = *filesystem_type,
        .mountpoint = *mountpoint,
        .service_process = service_process,
        .message_broker = message_broker,
        .filesystem = filesystem,
    };
}

static Result(UserspaceFilesystemPtr) UserspaceFilesystem__heap_new(
    ConstBStringPtr device_path,
    ConstBStringPtr filesystem_type,
    ConstBStringPtr mountpoint,
    ProcessPtr service_process,
    MessageBrokerPtr message_broker,
    FilesystemPtr filesystem
)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceFilesystem));
    if (!address.valid)
    {
        return Error(UserspaceFilesystemPtr, allocation_failed, SERVICE_DEVICE_REGISTRATION_FAILED);
    }

    UserspaceFilesystemPtr self = ptr(address.virtual_addr, UserspaceFilesystem);
    *self = UserspaceFilesystem__new(device_path, filesystem_type, mountpoint, service_process, message_broker, filesystem);
    return Success(UserspaceFilesystemPtr, self);
}

static UserspaceFilesystemRequestState UserspaceFilesystemRequestState__new(
    uint64_t request_id,
    UserspaceFilesystemPtr filesystem,
    ServiceFilesystemOperation operation,
    ConstBStringPtr path,
    IOInternalHandle handle,
    Flags flags,
    uint64_t block,
    uint64_t block_offset,
    const UserAddress * buffer,
    size_t size,
    const UserAddress * entries,
    size_t max_entries,
    const UserAddress * shared_buffer_allocation,
    bool has_shared_buffer,
    OperationKey wake_key,
    ProcessID waiting_pid
)
{
    return (UserspaceFilesystemRequestState) {
        .request_id = request_id,
        .filesystem = filesystem,
        .operation = operation,
        .path = *path,
        .handle = handle,
        .flags = flags,
        .block = block,
        .block_offset = block_offset,
        .buffer = *buffer,
        .size = size,
        .entries = *entries,
        .max_entries = max_entries,
        .shared_buffer_allocation = *shared_buffer_allocation,
        .has_shared_buffer = has_shared_buffer,
        .wake_key = wake_key,
        .completed = false,
        .status = SERVICE_EVENT_QUEUE_EMPTY,
        .result = 0,
        .mode = 0,
        .file_size = 0,
        .blocks = { 0 },
        .waiting_pid = Some(ProcessID, waiting_pid),
    };
}

static Result(UserspaceFilesystemRequestStatePtr) UserspaceFilesystemRequestState__heap_new(
    UserspaceFilesystemPtr filesystem,
    ServiceFilesystemOperation operation,
    ConstBStringPtr path,
    IOInternalHandle handle,
    Flags flags,
    uint64_t block,
    uint64_t block_offset,
    const UserAddress * buffer,
    size_t size,
    const UserAddress * entries,
    size_t max_entries,
    const UserAddress * shared_buffer_allocation,
    bool has_shared_buffer,
    OperationKey wake_key,
    ProcessID waiting_pid
)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceFilesystemRequestState));
    if (!address.valid)
    {
        return Error(UserspaceFilesystemRequestStatePtr, allocation_failed, SERVICE_FILESYSTEM_REQUEST_FAILED);
    }

    UserspaceFilesystemRequestStatePtr self = ptr(address.virtual_addr, UserspaceFilesystemRequestState);
    *self = UserspaceFilesystemRequestState__new(
        0,
        filesystem,
        operation,
        path,
        handle,
        flags,
        block,
        block_offset,
        buffer,
        size,
        entries,
        max_entries,
        shared_buffer_allocation,
        has_shared_buffer,
        wake_key,
        waiting_pid
    );
    return Success(UserspaceFilesystemRequestStatePtr, self);
}

static UserspaceFilesystemParkContext UserspaceFilesystemParkContext__new(KernelContext kernel_context, uint64_t request_id)
{
    return (UserspaceFilesystemParkContext) {
        .kernel_context = kernel_context,
        .request_id = request_id,
    };
}

static Result(UserspaceFilesystemParkContextPtr) UserspaceFilesystemParkContext__heap_new(
    KernelContext kernel_context, uint64_t request_id
)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceFilesystemParkContext));
    if (!address.valid)
    {
        return Error(UserspaceFilesystemParkContextPtr, allocation_failed, SERVICE_FILESYSTEM_REQUEST_FAILED);
    }

    UserspaceFilesystemParkContextPtr self = ptr(address.virtual_addr, UserspaceFilesystemParkContext);
    *self = UserspaceFilesystemParkContext__new(kernel_context, request_id);
    return Success(UserspaceFilesystemParkContextPtr, self);
}

static ServiceEvent UserspaceFilesystemRequestState__service_event(UserspaceFilesystemRequestStatePtr self)
{
    return ServiceEvent__new(SERVICE_EVENT_FILESYSTEM_IO, self->request_id, 0);
}

static ServiceFilesystemRequest UserspaceFilesystemRequestState__service_request(
    UserspaceFilesystemRequestStatePtr self
)
{
    return ServiceFilesystemRequest__new(
        self->request_id,
        self->operation,
        &self->filesystem->device_path,
        &self->path,
        self->handle,
        self->flags,
        self->block,
        self->block_offset,
        self->buffer.virtual_addr,
        self->size,
        self->entries.virtual_addr,
        self->max_entries
    );
}

static IOStatus UserspaceFilesystem__io_status_from_service_status(ServiceCode status)
{
    if (status == SERVICE_OPERATION_NOT_SUPPORTED)
    {
        return IO_OPERATION_NOT_SUPPORTED;
    }

    return IO_INVALID_PATH;
}

static UserspaceFilesystemIOStatePtr UserspaceFilesystem__io_state(UserspaceFilesystemPtr self)
{
    return (UserspaceFilesystemIOStatePtr) self->filesystem->state;
}

static Result(IOInternalHandle) UserspaceFilesystemIOState__add_handle(
    UserspaceFilesystemIOStatePtr self,
    ConstBStringPtr path,
    Flags flags,
    FileMode mode,
    uint64_t size,
    uint32_t * blocks
)
{
    critical_section(self->state.lock,
    {
        UserspaceFilesystemFileHandle handle;
        handle.path = *path;
        handle.handle = self->next_handle++ | self->state.filesystem->fs_code;
        handle.flags = flags;
        handle.mode = mode;
        handle.size = size;
        handle.offset = 0;

        for (size_t i = 0; i < 12; i++)
        {
            handle.block[i] = blocks[i];
        }

        match_result(CollectionCode, UserspaceFilesystemHandleMap__set(&self->handles, handle.handle, handle),
            success(_) { cs_return Success(IOInternalHandle, handle.handle); },
            error(code, message) { cs_return Error(IOInternalHandle, message, IO_REGISTER_FAILED); }
        );
    });

    return Error(IOInternalHandle, request_failed, IO_REGISTER_FAILED);
}

static Result(UserspaceFilesystemFileHandlePtr) UserspaceFilesystemIOState__get_handle(
    UserspaceFilesystemIOStatePtr self,
    IOInternalHandle handle
)
{
    critical_section(self->state.lock,
    {
        match_optional(
            UserspaceFilesystemHandleMap__value_type_ptr,
            UserspaceFilesystemHandleMap__get_ptr(&self->handles, handle),
            some(file_handle) { cs_return Success(UserspaceFilesystemFileHandlePtr, file_handle); },
            none() { cs_return Error(UserspaceFilesystemFileHandlePtr, request_not_found, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(UserspaceFilesystemFileHandlePtr, request_not_found, IO_UNKNOWN_HANDLE);
}

static Result(IOInternalHandle) UserspaceFilesystemIOState__delete_handle(
    UserspaceFilesystemIOStatePtr self,
    IOInternalHandle handle
)
{
    critical_section(self->state.lock,
    {
        match_result(CollectionCode, UserspaceFilesystemHandleMap__delete(&self->handles, handle),
            success(_) { cs_return Success(IOInternalHandle, handle); },
            error(code, message) { cs_return Error(IOInternalHandle, request_not_found, IO_UNKNOWN_HANDLE); }
        );
    });

    return Error(IOInternalHandle, request_not_found, IO_UNKNOWN_HANDLE);
}

static UserspaceFilesystemRequestStart UserspaceFilesystemRequestStart__new(
    UserspaceFilesystemRequestStatePtr request,
    UserspaceFilesystemParkContextPtr park_context,
    ServiceEvent event
)
{
    return (UserspaceFilesystemRequestStart) {
        .request = request,
        .park_context = park_context,
        .event = event,
    };
}

static void UserspaceFilesystemRequestState__release(UserspaceFilesystemRequestStatePtr self)
{
    if (self->has_shared_buffer)
    {
        match_result(ProcessMemoryCode, Process__free_user_pages(
            self->filesystem->service_process, self->shared_buffer_allocation.virtual_addr
        ),
            success_do_nothing(),
            error(code, message)
            {
                __d_printf(FILESYSTEM, "userspace fs request free failed id=%i code=%i\n", (int) self->request_id, (int) code);
            }
        );
    }

    Kernel__free(addr(self));
}

static void UserspaceFilesystemRegistry__remove_request(
    UserspaceFilesystemRegistryPtr self, UserspaceFilesystemRequestStatePtr request
)
{
    critical_section(self->lock,
        match_result(CollectionCode, UserspaceFilesystemRequestMap__delete(&self->requests, request->request_id),
            success_do_nothing(),
            error(code, message)
            {
                __d_printf(
                    FILESYSTEM,
                    "userspace fs request delete failed id=%i code=%i\n",
                    (int) request->request_id,
                    (int) code
                );
            }
        );
    );
}

static Result(ServiceCode) UserspaceFilesystem__deliver_service_event(
    UserspaceFilesystemPtr self, ServiceEvent event
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

static Result(Anything) UserspaceFilesystem__complete_wait(ParkedOperationPtr parked_operation)
{
    UserspaceFilesystemParkContextPtr park_context =
        (UserspaceFilesystemParkContextPtr) parked_operation->kernel_context;
    UserspaceFilesystemRegistryPtr registry = UserspaceFilesystemRegistry__shared();
    Optional(UserspaceFilesystemRequestStatePtr) completed_request = None(UserspaceFilesystemRequestStatePtr);
    Result(Anything) wait_result = Error(Anything, request_not_found, IO_INVALID_PATH);

    critical_section(registry->lock,
        match_optional(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestMap__get(
            &registry->requests, park_context->request_id
        ),
            some(request)
            {
                if (!request->completed)
                {
                    wait_result = Error(Anything, request_not_complete, IO_WAIT_FOR_DATA);
                }
                else
                {
                    match_result(CollectionCode, UserspaceFilesystemRequestMap__delete(
                        &registry->requests, request->request_id
                    ),
                        success_do_nothing(),
                        error(code, message)
                        {
                            __d_printf(
                                FILESYSTEM,
                                "userspace fs request delete failed id=%i code=%i\n",
                                (int) request->request_id,
                                (int) code
                            );
                        }
                    );
                    completed_request = Some(UserspaceFilesystemRequestStatePtr, request);
                }
            },
            none()
            {
                wait_result = Error(Anything, request_not_found, IO_INVALID_PATH);
            }
        );
    );

    match_optional(UserspaceFilesystemRequestStatePtr, completed_request,
        some(request)
        {
            ServiceCode status = request->status;
            ServiceFilesystemOperation operation = request->operation;
            int64_t result = request->result;
            IOInternalHandle handle = request->handle;
            Flags flags = request->flags;
            UserspaceFilesystemPtr filesystem = request->filesystem;
            FileMode mode = request->mode;
            uint64_t file_size = request->file_size;
            BString path = request->path;
            uint32_t blocks[12] = { 0 };
            for (size_t i = 0; i < 12; i++)
            {
                blocks[i] = request->blocks[i];
            }
            UserspaceFilesystemRequestState__release(request);

            if (status != SERVICE_OK)
            {
                Result(Anything) mapped_result =
                    Error(Anything, request_failed, UserspaceFilesystem__io_status_from_service_status(status));
                return KernelContext__map_result(
                    &park_context->kernel_context,
                    &mapped_result
                );
            }

            if (operation == SERVICE_FILESYSTEM_OPEN)
            {
                match_result(IOInternalHandle, UserspaceFilesystemIOState__add_handle(
                    UserspaceFilesystem__io_state(filesystem),
                    &path,
                    flags,
                    mode,
                    file_size,
                    blocks
                ),
                    success(open_handle)
                    {
                        Result(Anything) mapped_result = Success(Anything, open_handle);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    },
                    error(code, message)
                    {
                        Result(Anything) mapped_result = Error(Anything, message, code);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    }
                );
            }

            if (operation == SERVICE_FILESYSTEM_READ)
            {
                match_result(UserspaceFilesystemFileHandlePtr, UserspaceFilesystemIOState__get_handle(
                    UserspaceFilesystem__io_state(filesystem), handle
                ),
                    success(file_handle)
                    {
                        critical_section(UserspaceFilesystem__io_state(filesystem)->state.lock,
                            file_handle->offset += (uint64_t) result;
                        );
                        Result(Anything) mapped_result = Success(Anything, result);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    },
                    error(code, message)
                    {
                        Result(Anything) mapped_result = Error(Anything, message, code);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    }
                );
            }

            if (operation == SERVICE_FILESYSTEM_WRITE)
            {
                match_result(UserspaceFilesystemFileHandlePtr, UserspaceFilesystemIOState__get_handle(
                    UserspaceFilesystem__io_state(filesystem), handle
                ),
                    success(file_handle)
                    {
                        critical_section(UserspaceFilesystem__io_state(filesystem)->state.lock,
                            file_handle->mode = mode;
                            file_handle->size = file_size;
                            file_handle->offset += (uint64_t) result;
                            for (size_t i = 0; i < 12; i++)
                            {
                                file_handle->block[i] = blocks[i];
                            }
                        );
                        Result(Anything) mapped_result = Success(Anything, result);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    },
                    error(code, message)
                    {
                        Result(Anything) mapped_result = Error(Anything, message, code);
                        return KernelContext__map_result(
                            &park_context->kernel_context,
                            &mapped_result
                        );
                    }
                );
            }

            Result(Anything) mapped_result = Success(Anything, result);
            return KernelContext__map_result(&park_context->kernel_context, &mapped_result);
        },
        none_do_nothing()
    );

    return wait_result;
}

static Result(UserspaceFilesystemRequestStart) UserspaceFilesystemRegistry__start_request(
    UserspaceFilesystemRegistryPtr self,
    UserspaceFilesystemRequestStatePtr request,
    KernelContext kernel_context
)
{
    Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
    critical_section(self->lock,
        request->request_id = self->next_request_id++;
        if (request->wake_key == 0)
        {
            request->wake_key = request->request_id;
        }
        set_result = UserspaceFilesystemRequestMap__set(&self->requests, request->request_id, request);
    );

    match_result(CollectionCode, set_result,
        success_do_nothing(),
        error(code, message)
        {
            UserspaceFilesystemRequestState__release(request);
            return Error(UserspaceFilesystemRequestStart, registry_full, SERVICE_FILESYSTEM_REQUEST_FAILED);
        }
    );

    match_result(UserspaceFilesystemParkContextPtr, UserspaceFilesystemParkContext__heap_new(
        kernel_context, request->request_id
    ),
        success(park_context)
        {
            ServiceEvent event = UserspaceFilesystemRequestState__service_event(request);
            return Success(
                UserspaceFilesystemRequestStart,
                UserspaceFilesystemRequestStart__new(request, park_context, event)
            );
        },
        error(code, message)
        {
            UserspaceFilesystemRegistry__remove_request(self, request);
            UserspaceFilesystemRequestState__release(request);
            return Error(UserspaceFilesystemRequestStart, message, SERVICE_FILESYSTEM_REQUEST_FAILED);
        }
    );

    return Error(UserspaceFilesystemRequestStart, request_failed, SERVICE_FILESYSTEM_REQUEST_FAILED);
}

static Result(ServiceCode) UserspaceFilesystem__submit_started_request(
    UserspaceFilesystemIOStatePtr state,
    UserspaceFilesystemRegistryPtr registry,
    ProcessID pid,
    ConstUserspaceFilesystemRequestStartPtr start
)
{
    match_result(ServiceCode, UserspaceFilesystem__deliver_service_event(state->filesystem, start->event),
        success_do_nothing(),
        error(code, message)
        {
            UserspaceFilesystemRegistry__remove_request(registry, start->request);
            UserspaceFilesystemRequestState__release(start->request);
            Kernel__free(addr(start->park_context));
            return Error(ServiceCode, message, SERVICE_FILESYSTEM_REQUEST_FAILED);
        }
    );

    MessagePtr message =
        Message__heap_new_deferred_with_context_ownership_and_operation_key(
            SOURCE_IO,
            SUBSYS_IO_PARK,
            pid,
            &start->park_context->kernel_context,
            UserspaceFilesystem__complete_wait,
            true,
            &Some(OperationKey, start->request->wake_key)
        );
    match_result(AckCode, MessageBroker__deliver(state->filesystem->message_broker, message),
        success(_) { return Success(ServiceCode, SERVICE_OK); },
        error(code, message)
        {
            UserspaceFilesystemRegistry__remove_request(registry, start->request);
            UserspaceFilesystemRequestState__release(start->request);
            Kernel__free(addr(start->park_context));
            return Error(ServiceCode, message, SERVICE_FILESYSTEM_REQUEST_FAILED);
        }
    );

    return Error(ServiceCode, request_failed, SERVICE_FILESYSTEM_REQUEST_FAILED);
}

static Result(IOInternalHandle) UserspaceFilesystem__open(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    UserspaceFilesystemIOStatePtr state = (UserspaceFilesystemIOStatePtr) self;

    if ((flags & IO_OPEN_WRITE) != 0 && (flags & IO_OPEN_READ) == 0)
    {
        uint32_t blocks[12] = { 0 };
        return UserspaceFilesystemIOState__add_handle(state, path, flags, IO_MODE_TYPE_FILE | 0644, 0, blocks);
    }

    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context)
        {
            match_optional(ProcessID, kernel_context->pid,
	                some(pid)
	                {
	                    UserAddress null_user_address = NULL_USER_ADDRESS;
	                    match_result(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestState__heap_new(
                        state->filesystem,
                        SERVICE_FILESYSTEM_OPEN,
                        path,
                        0,
                        flags,
                        0,
                        0,
	                        &null_user_address,
                        0,
	                        &null_user_address,
                        0,
	                        &null_user_address,
                        false,
                        0,
                        pid
                    ),
                        success(request)
                        {
                            UserspaceFilesystemRegistryPtr registry = UserspaceFilesystemRegistry__shared();
                            match_result(UserspaceFilesystemRequestStart, UserspaceFilesystemRegistry__start_request(
                                registry, request, *kernel_context
                            ),
                                success(start)
                                {
                                    match_result(ServiceCode, UserspaceFilesystem__submit_started_request(
                                        state, registry, pid, &start
                                    ),
                                        success(_) { return Error(IOInternalHandle, request_pending, IO_WAIT_FOR_DATA); },
                                        error(code, message) { return Error(IOInternalHandle, message, IO_INVALID_PATH); }
                                    );
                                },
                                error(code, message) { return Error(IOInternalHandle, message, IO_INVALID_PATH); }
                            );
                        },
                        error(code, message) { return Error(IOInternalHandle, message, IO_INVALID_PATH); }
                    );
                },
                none() { return Error(IOInternalHandle, invalid_argument, IO_INVALID_PATH); }
            );
        },
        none() { return Error(IOInternalHandle, invalid_argument, IO_INVALID_PATH); }
    );

    return Error(IOInternalHandle, invalid_argument, IO_INVALID_PATH);
}

static Result(IOInternalHandle) UserspaceFilesystem__close(IOStatePtr self, IOInternalHandle handle)
{
    return UserspaceFilesystemIOState__delete_handle((UserspaceFilesystemIOStatePtr) self, handle);
}

static Result(IOBufferPtr) UserspaceFilesystem__read(
    IOStatePtr self, IOInternalHandle handle, size_t size, ConstExternalBufferPtr target
)
{
    UserspaceFilesystemIOStatePtr state = (UserspaceFilesystemIOStatePtr) self;

    match_result(UserspaceFilesystemFileHandlePtr, UserspaceFilesystemIOState__get_handle(state, handle),
        success(file_handle)
        {
            if ((file_handle->flags & IO_OPEN_READ) == 0)
            {
                return Error(IOBufferPtr, request_failed, IO_OPERATION_NOT_SUPPORTED);
            }

            if ((file_handle->mode & IO_MODE_TYPE_MASK) != IO_MODE_TYPE_FILE)
            {
                return Error(IOBufferPtr, request_failed, IO_INVALID_PATH);
            }

            if (file_handle->offset >= file_handle->size)
            {
                return Error(IOBufferPtr, request_not_found, IO_EOF);
            }

            size_t block_index = file_handle->offset / 1024U;
            if (block_index >= 12 || file_handle->block[block_index] == 0)
            {
                return Error(IOBufferPtr, request_failed, IO_INVALID_PATH);
            }

            uint64_t block_offset = file_handle->offset % 1024U;
            size_t remaining_file = (size_t) (file_handle->size - file_handle->offset);
            size_t remaining_block = 1024U - (size_t) block_offset;
            size_t read_size = size;
            if (read_size > target->len)
            {
                read_size = target->len;
            }
            if (read_size > remaining_file)
            {
                read_size = remaining_file;
            }
            if (read_size > remaining_block)
            {
                read_size = remaining_block;
            }

            match_optional(KernelContextPtr, BlockDevice__current_context(),
                some(kernel_context)
                {
                    match_optional(ProcessID, kernel_context->pid,
                        some(pid)
                        {
                            match_optional(SchedulerPtr, kernel_context->scheduler,
                                some(scheduler)
                                {
                                    match_optional(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid),
                                        some(requester)
                                        {
                                            match_result(SharedUserPages, Process__map_shared_user_pages_with_allocation(
                                                state->filesystem->service_process,
                                                requester,
                                                addr(target->data),
                                                read_size
                                            ),
	                                                success(shared)
	                                                {
	                                                    UserAddress null_user_address = NULL_USER_ADDRESS;
	                                                    match_result(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestState__heap_new(
                                                        state->filesystem,
                                                        SERVICE_FILESYSTEM_READ,
	                                                        &bstr(""),
                                                        handle,
                                                        file_handle->flags,
                                                        file_handle->block[block_index],
                                                        block_offset,
                                                        &shared.mapping,
                                                        read_size,
	                                                        &null_user_address,
                                                        0,
                                                        &shared.allocation,
                                                        true,
                                                        0,
                                                        pid
                                                    ),
                                                        success(request)
                                                        {
                                                            UserspaceFilesystemRegistryPtr registry =
                                                                UserspaceFilesystemRegistry__shared();
                                                            match_result(UserspaceFilesystemRequestStart, UserspaceFilesystemRegistry__start_request(
                                                                registry, request, *kernel_context
                                                            ),
                                                                success(start)
                                                                {
                                                                    match_result(ServiceCode, UserspaceFilesystem__submit_started_request(
                                                                        state, registry, pid, &start
                                                                    ),
                                                                        success(_) { return Error(IOBufferPtr, request_pending, IO_WAIT_FOR_DATA); },
                                                                        error(code, message) { return Error(IOBufferPtr, message, IO_INVALID_PATH); }
                                                                    );
                                                                },
                                                                error(code, message) { return Error(IOBufferPtr, message, IO_INVALID_PATH); }
                                                            );
                                                        },
                                                        error(code, message)
                                                        {
                                                            match_result(ProcessMemoryCode, Process__free_user_pages(
                                                                state->filesystem->service_process,
                                                                shared.allocation.virtual_addr
                                                            ),
                                                                success_do_nothing(),
                                                                error(free_code, free_message)
                                                                {
                                                                    __d_printf(
                                                                        FILESYSTEM,
                                                                        "userspace fs mapped buffer cleanup failed code=%i\n",
                                                                        (int) free_code
                                                                    );
                                                                }
                                                            );
                                                            return Error(IOBufferPtr, message, IO_INVALID_PATH);
                                                        }
                                                    );
                                                },
                                                error(code, message) { return Error(IOBufferPtr, message, IO_INVALID_PATH); }
                                            );
                                        },
                                        none() { return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH); }
                                    );
                                },
                                none() { return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH); }
                            );
                        },
                        none() { return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH); }
                    );
                },
                none() { return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH); }
            );
        },
        error(code, message) { return Error(IOBufferPtr, message, code); }
    );

    return Error(IOBufferPtr, invalid_argument, IO_INVALID_PATH);
}

static Result(ByteCount) UserspaceFilesystem__write(
    IOStatePtr self, IOInternalHandle handle, DataPtr data, size_t size
)
{
    UserspaceFilesystemIOStatePtr state = (UserspaceFilesystemIOStatePtr) self;

    match_result(UserspaceFilesystemFileHandlePtr, UserspaceFilesystemIOState__get_handle(state, handle),
        success(file_handle)
        {
            if ((file_handle->flags & IO_OPEN_WRITE) == 0 || file_handle->offset != 0)
            {
                return Error(ByteCount, request_failed, IO_OPERATION_NOT_SUPPORTED);
            }

            match_optional(KernelContextPtr, BlockDevice__current_context(),
                some(kernel_context)
                {
                    match_optional(ProcessID, kernel_context->pid,
                        some(pid)
                        {
                            match_optional(SchedulerPtr, kernel_context->scheduler,
                                some(scheduler)
                                {
                                    match_optional(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid),
                                        some(requester)
                                        {
                                            match_result(SharedUserPages, Process__map_shared_user_pages_with_allocation(
                                                state->filesystem->service_process,
                                                requester,
                                                addr(data),
                                                size
                                            ),
	                                                success(shared)
	                                                {
	                                                    UserAddress null_user_address = NULL_USER_ADDRESS;
	                                                    match_result(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestState__heap_new(
                                                        state->filesystem,
                                                        SERVICE_FILESYSTEM_WRITE,
                                                        &file_handle->path,
                                                        handle,
                                                        file_handle->flags,
                                                        0,
                                                        0,
                                                        &shared.mapping,
                                                        size,
	                                                        &null_user_address,
                                                        0,
                                                        &shared.allocation,
                                                        true,
                                                        0,
                                                        pid
                                                    ),
                                                        success(request)
                                                        {
                                                            UserspaceFilesystemRegistryPtr registry =
                                                                UserspaceFilesystemRegistry__shared();
                                                            match_result(UserspaceFilesystemRequestStart, UserspaceFilesystemRegistry__start_request(
                                                                registry, request, *kernel_context
                                                            ),
                                                                success(start)
                                                                {
                                                                    match_result(ServiceCode, UserspaceFilesystem__submit_started_request(
                                                                        state, registry, pid, &start
                                                                    ),
                                                                        success(_) { return Error(ByteCount, request_pending, IO_WAIT_FOR_DATA); },
                                                                        error(code, message) { return Error(ByteCount, message, IO_INVALID_PATH); }
                                                                    );
                                                                },
                                                                error(code, message) { return Error(ByteCount, message, IO_INVALID_PATH); }
                                                            );
                                                        },
                                                        error(code, message)
                                                        {
                                                            match_result(ProcessMemoryCode, Process__free_user_pages(
                                                                state->filesystem->service_process,
                                                                shared.allocation.virtual_addr
                                                            ),
                                                                success_do_nothing(),
                                                                error(free_code, free_message)
                                                                {
                                                                    __d_printf(
                                                                        FILESYSTEM,
                                                                        "userspace fs write buffer cleanup failed code=%i\n",
                                                                        (int) free_code
                                                                    );
                                                                }
                                                            );
                                                            return Error(ByteCount, message, IO_INVALID_PATH);
                                                        }
                                                    );
                                                },
                                                error(code, message) { return Error(ByteCount, message, IO_INVALID_PATH); }
                                            );
                                        },
                                        none() { return Error(ByteCount, invalid_argument, IO_INVALID_PATH); }
                                    );
                                },
                                none() { return Error(ByteCount, invalid_argument, IO_INVALID_PATH); }
                            );
                        },
                        none() { return Error(ByteCount, invalid_argument, IO_INVALID_PATH); }
                    );
                },
                none() { return Error(ByteCount, invalid_argument, IO_INVALID_PATH); }
            );
        },
        error(code, message) { return Error(ByteCount, message, code); }
    );

    return Error(ByteCount, invalid_argument, IO_INVALID_PATH);
}

static Result(IOStatus) UserspaceFilesystem__status_operation(
    IOStatePtr self, ConstBStringPtr path, Flags flags, ServiceFilesystemOperation operation
)
{
    UserspaceFilesystemIOStatePtr state = (UserspaceFilesystemIOStatePtr) self;

    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context)
        {
            match_optional(ProcessID, kernel_context->pid,
	                some(pid)
	                {
	                    UserAddress null_user_address = NULL_USER_ADDRESS;
	                    match_result(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestState__heap_new(
                        state->filesystem,
                        operation,
                        path,
                        0,
                        flags,
                        0,
                        0,
	                        &null_user_address,
                        0,
	                        &null_user_address,
                        0,
	                        &null_user_address,
                        false,
                        0,
                        pid
                    ),
                        success(request)
                        {
                            UserspaceFilesystemRegistryPtr registry = UserspaceFilesystemRegistry__shared();
                            match_result(UserspaceFilesystemRequestStart, UserspaceFilesystemRegistry__start_request(
                                registry, request, *kernel_context
                            ),
                                success(start)
                                {
                                    match_result(ServiceCode, UserspaceFilesystem__submit_started_request(
                                        state, registry, pid, &start
                                    ),
                                        success(_) { return Error(IOStatus, request_pending, IO_WAIT_FOR_DATA); },
                                        error(code, message) { return Error(IOStatus, message, IO_INVALID_PATH); }
                                    );
                                },
                                error(code, message) { return Error(IOStatus, message, IO_INVALID_PATH); }
                            );
                        },
                        error(code, message) { return Error(IOStatus, message, IO_INVALID_PATH); }
                    );
                },
                none() { return Error(IOStatus, invalid_argument, IO_INVALID_PATH); }
            );
        },
        none() { return Error(IOStatus, invalid_argument, IO_INVALID_PATH); }
    );

    return Error(IOStatus, invalid_argument, IO_INVALID_PATH);
}

static Result(IOStatus) UserspaceFilesystem__mkdir(IOStatePtr self, ConstBStringPtr path)
{
    return UserspaceFilesystem__status_operation(self, path, 0, SERVICE_FILESYSTEM_MKDIR);
}

static Result(IOStatus) UserspaceFilesystem__remove(IOStatePtr self, ConstBStringPtr path, Flags flags)
{
    return UserspaceFilesystem__status_operation(self, path, flags, SERVICE_FILESYSTEM_REMOVE);
}

static Result(DirectoryEntryCount) UserspaceFilesystem__list_dir(
    IOStatePtr self, ConstBStringPtr path, DirectoryEntryPtr entries, size_t max_entries
)
{
    UserspaceFilesystemIOStatePtr state = (UserspaceFilesystemIOStatePtr) self;

    if (max_entries == 0)
    {
        return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
    }

    match_optional(KernelContextPtr, BlockDevice__current_context(),
        some(kernel_context)
        {
            match_optional(ProcessID, kernel_context->pid,
                some(pid)
                {
                    match_optional(SchedulerPtr, kernel_context->scheduler,
                        some(scheduler)
                        {
                            match_optional(ProcessPtr, Scheduler__find_process_by_pid(scheduler, pid),
                                some(requester)
                                {
                                    size_t buffer_size = max_entries * sizeof(DirectoryEntry);
                                    match_result(SharedUserPages, Process__map_shared_user_pages_with_allocation(
                                        state->filesystem->service_process,
                                        requester,
                                        addr(entries),
                                        buffer_size
                                    ),
	                                        success(shared)
	                                        {
	                                            UserAddress null_user_address = NULL_USER_ADDRESS;
	                                            UserspaceFilesystemRegistryPtr registry =
                                                UserspaceFilesystemRegistry__shared();
                                            match_result(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestState__heap_new(
                                                state->filesystem,
                                                SERVICE_FILESYSTEM_LIST_DIR,
                                                path,
                                                0,
                                                0,
                                                0,
                                                0,
	                                                &null_user_address,
                                                0,
                                                &shared.mapping,
                                                max_entries,
                                                &shared.allocation,
                                                true,
                                                0,
                                                pid
                                            ),
                                                success(request)
                                                {
                                                    match_result(UserspaceFilesystemRequestStart, UserspaceFilesystemRegistry__start_request(
                                                        registry, request, *kernel_context
                                                    ),
                                                        success(start)
                                                        {
                                                            match_result(ServiceCode, UserspaceFilesystem__submit_started_request(
                                                                state, registry, pid, &start
                                                            ),
                                                                success(_) { return Error(DirectoryEntryCount, request_pending, IO_WAIT_FOR_DATA); },
                                                                error(code, message) { return Error(DirectoryEntryCount, message, IO_INVALID_PATH); }
                                                            );
                                                        },
                                                        error(code, message) { return Error(DirectoryEntryCount, message, IO_INVALID_PATH); }
                                                    );
                                                },
                                                error(code, message)
                                                {
                                                    match_result(ProcessMemoryCode, Process__free_user_pages(
                                                        state->filesystem->service_process,
                                                        shared.allocation.virtual_addr
                                                    ),
                                                        success_do_nothing(),
                                                        error(free_code, free_message)
                                                        {
                                                            __d_printf(
                                                                FILESYSTEM,
                                                                "userspace fs mapped entries cleanup failed code=%i\n",
                                                                (int) free_code
                                                            );
                                                        }
                                                    );
                                                    return Error(DirectoryEntryCount, message, IO_INVALID_PATH);
                                                }
                                            );
                                        },
                                        error(code, message) { return Error(DirectoryEntryCount, message, IO_INVALID_PATH); }
                                    );
                                },
                                none()
                                {
                                    return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
                                }
                            );
                        },
                        none()
                        {
                            return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
                        }
                    );
                },
                none()
                {
                    return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
                }
            );
        },
        none()
        {
            return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
        }
    );

    return Error(DirectoryEntryCount, invalid_argument, IO_INVALID_PATH);
}

static FileIO UserspaceFilesystemIO__new(void)
{
    return (FileIO) {
        .base_io = IO__new(
            UserspaceFilesystem__open,
            UserspaceFilesystem__read,
            UserspaceFilesystem__write,
            IO__unsupported_accept,
            UserspaceFilesystem__close,
            IO__unsupported_park,
            IO__unsupported_unpark,
            IO__unsupported_server_buffer,
            IO__unsupported_server_write_buffer,
            IO__unsupported_data_available,
            IO__unsupported_data_consumed,
            IO__unsupported_eof
        ),
        .list_nodes = FileIO__unsupported_list_nodes,
        .list_dir = UserspaceFilesystem__list_dir,
        .mkdir = UserspaceFilesystem__mkdir,
        .remove = UserspaceFilesystem__remove,
        .link = FileIO__unsupported_link,
    };
}

static UserspaceFilesystemIOState UserspaceFilesystemIOState__new(
    FilesystemPtr filesystem, UserspaceFilesystemPtr userspace_filesystem
)
{
    return (UserspaceFilesystemIOState) {
        .state = FilesystemIOState__new(filesystem),
        .filesystem = userspace_filesystem,
        .handles = UserspaceFilesystemHandleMap__new(),
        .next_handle = 1,
    };
}

static Result(UserspaceFilesystemIOStatePtr) UserspaceFilesystemIOState__heap_new(
    FilesystemPtr filesystem, UserspaceFilesystemPtr userspace_filesystem
)
{
    KernelAddress address = Kernel__alloc(sizeof(UserspaceFilesystemIOState));
    if (!address.valid)
    {
        return Error(UserspaceFilesystemIOStatePtr, allocation_failed, SERVICE_DEVICE_REGISTRATION_FAILED);
    }

    UserspaceFilesystemIOStatePtr self = ptr(address.virtual_addr, UserspaceFilesystemIOState);
    *self = UserspaceFilesystemIOState__new(filesystem, userspace_filesystem);
    filesystem->state = &self->state;
    return Success(UserspaceFilesystemIOStatePtr, self);
}

Result(ServiceCode) UserspaceFilesystemRegistry__register(
    UserspaceFilesystemRegistryPtr self,
    ProcessPtr service_process,
    ServiceFilesystemRegistrationPtr registration,
    FilesystemPtr root_filesystem,
    MessageBrokerPtr message_broker
)
{
    FileIO file_io = UserspaceFilesystemIO__new();
    FilesystemPtr mounted_filesystem = Filesystem__heap_new(&registration->mountpoint, &file_io);
    match_result(UserspaceFilesystemPtr, UserspaceFilesystem__heap_new(
        &registration->device_path,
        &registration->filesystem_type,
        &registration->mountpoint,
        service_process,
        message_broker,
        mounted_filesystem
    ),
        success(userspace_filesystem)
        {
            match_result(UserspaceFilesystemIOStatePtr, UserspaceFilesystemIOState__heap_new(
                mounted_filesystem, userspace_filesystem
            ),
                success_do_nothing(),
                error(code, message)
                {
                    Kernel__free(addr(userspace_filesystem));
                    Kernel__free(addr(mounted_filesystem));
                    return Error(ServiceCode, message, SERVICE_DEVICE_REGISTRATION_FAILED);
                }
            );

            Result(CollectionCode) set_result = Error(CollectionCode, registry_full, MAP_FULL);
            critical_section(self->lock,
                set_result = UserspaceFilesystemRegistryMap__set(
                    &self->filesystems, registration->mountpoint, userspace_filesystem
                );
            );

            match_result(CollectionCode, set_result,
                success_do_nothing(),
                error(code, message)
                {
                    Kernel__free(addr(userspace_filesystem));
                    Kernel__free(addr(mounted_filesystem->state));
                    Kernel__free(addr(mounted_filesystem));
                    return Error(ServiceCode, registry_full, SERVICE_DEVICE_REGISTRATION_FAILED);
                }
            );

            match_result(MountResult, Filesystem__mount(root_filesystem, &registration->mountpoint, mounted_filesystem),
                success(_) { return Success(ServiceCode, SERVICE_OK); },
                error(code, message)
                {
                    critical_section(self->lock,
                        match_result(CollectionCode, UserspaceFilesystemRegistryMap__delete(
                            &self->filesystems, registration->mountpoint
                        ),
                            success_do_nothing(),
                            error(delete_code, delete_message)
                            {
                                __d_printf(
                                    FILESYSTEM,
                                    "userspace fs registry cleanup failed code=%i\n",
                                    (int) delete_code
                                );
                            }
                        );
                    );
                    Kernel__free(addr(userspace_filesystem));
                    Kernel__free(addr(mounted_filesystem->state));
                    Kernel__free(addr(mounted_filesystem));
                    return Error(ServiceCode, message, SERVICE_DEVICE_REGISTRATION_FAILED);
                }
            );
        },
        error(code, message) { return Error(ServiceCode, message, code); }
    );

    return Error(ServiceCode, request_failed, SERVICE_DEVICE_REGISTRATION_FAILED);
}

Result(ServiceFilesystemRequest) UserspaceFilesystemRegistry__get_request(
    UserspaceFilesystemRegistryPtr self,
    ProcessPtr service_process,
    uint64_t request_id
)
{
    Result(ServiceFilesystemRequest) request_result =
        Error(ServiceFilesystemRequest, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);

    critical_section(self->lock,
        match_optional(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestMap__get(&self->requests, request_id),
            some(request)
            {
                if (request->filesystem->service_process != service_process)
                {
                    request_result = Error(ServiceFilesystemRequest, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);
                }
                else
                {
                    request_result = Success(
                        ServiceFilesystemRequest,
                        UserspaceFilesystemRequestState__service_request(request)
                    );
                }
            },
            none()
            {
                request_result = Error(ServiceFilesystemRequest, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);
            }
        );
    );

    return request_result;
}

Result(ServiceCode) UserspaceFilesystemRegistry__complete(
    UserspaceFilesystemRegistryPtr self,
    ProcessPtr service_process,
    ServiceFilesystemCompletionPtr completion
)
{
    Optional(ProcessID) waiting_pid = None(ProcessID);
    Optional(MessageBrokerPtr) message_broker = None(MessageBrokerPtr);
    OperationKey wake_key = 0;
    Result(ServiceCode) completion_result = Error(ServiceCode, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);

    critical_section(self->lock,
        match_optional(UserspaceFilesystemRequestStatePtr, UserspaceFilesystemRequestMap__get(
            &self->requests, completion->request_id
        ),
            some(request)
            {
                if (request->filesystem->service_process != service_process)
                {
                    completion_result = Error(ServiceCode, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);
                }
                else if (request->operation == SERVICE_FILESYSTEM_LIST_DIR
                    && (completion->result < 0 || (size_t) completion->result > request->max_entries))
                {
                    completion_result = Error(ServiceCode, invalid_argument, SERVICE_FILESYSTEM_REQUEST_FAILED);
                }
                else if (request->operation == SERVICE_FILESYSTEM_READ
                    && (completion->result < 0 || (size_t) completion->result > request->size))
                {
                    completion_result = Error(ServiceCode, invalid_argument, SERVICE_FILESYSTEM_REQUEST_FAILED);
                }
                else if (request->operation == SERVICE_FILESYSTEM_WRITE
                    && (completion->result < 0 || (size_t) completion->result > request->size))
                {
                    completion_result = Error(ServiceCode, invalid_argument, SERVICE_FILESYSTEM_REQUEST_FAILED);
                }
                else
                {
                    request->completed = true;
                    request->status = completion->status;
                    request->result = completion->result;
                    request->mode = completion->mode;
                    request->file_size = completion->size;
                    for (size_t i = 0; i < 12; i++)
                    {
                        request->blocks[i] = completion->block[i];
                    }
                    waiting_pid = request->waiting_pid;
                    message_broker = Some(MessageBrokerPtr, request->filesystem->message_broker);
                    wake_key = request->wake_key;
                    completion_result = Success(ServiceCode, SERVICE_OK);
                }
            },
            none()
            {
                completion_result = Error(ServiceCode, request_not_found, SERVICE_FILESYSTEM_REQUEST_NOT_FOUND);
            }
        );
    );

    match_result(ServiceCode, completion_result,
        success_do_nothing(),
        error(code, message) { return Error(ServiceCode, message, code); }
    );

    match_optional(ProcessID, waiting_pid,
        some(pid)
        {
            match_optional(MessageBrokerPtr, message_broker,
                some(broker)
                {
                    MessagePtr message = Message__heap_new_io_operation(
                        SOURCE_IO, SUBSYS_IO_OPERATION_AVAILABLE, pid, wake_key
                    );
                    match_result(AckCode, MessageBroker__deliver(broker, message),
                        success_do_nothing(),
                        error(code, message) { return Error(ServiceCode, message, SERVICE_SCHEDULER_ERROR); }
                    );
                },
                none()
                {
                    return Error(ServiceCode, request_failed, SERVICE_SCHEDULER_ERROR);
                }
            );
        },
        none_do_nothing()
    );

    return Success(ServiceCode, SERVICE_OK);
}

void UserspaceFilesystemRegistry__reset(UserspaceFilesystemRegistryPtr self)
{
    critical_section(self->lock,
        for_each_map(UserspaceFilesystemRegistryMap, self->filesystems, index, mountpoint, filesystem,
        {
            Kernel__free(addr(filesystem));
        });

        for_each_map(UserspaceFilesystemRequestMap, self->requests, request_index, request_id, request,
        {
            Kernel__free(addr(request));
        });

        self->filesystems = UserspaceFilesystemRegistryMap__new();
        self->requests = UserspaceFilesystemRequestMap__new();
        self->next_request_id = 1;
    );
}

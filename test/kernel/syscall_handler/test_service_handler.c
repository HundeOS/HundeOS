#include "unity.h"

#include "../../../kernel/syscall/service_handler.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/scheduler/context_switch.h"
#include "../../../kernel/scheduler/register.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/kalloc.h"
#include "../../../kernel/subsystems/io/block/block_device.h"
#include "../../../kernel/subsystems/io/file/memfs/memfs.h"
#include "../../../common_includes/syscalls/service.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;
static bool async_continuation_called = false;
static BlockCount async_continuation_blocks = 0;
static bool async_continuation_has_buffer = false;
static bool async_write_continuation_called = false;
static bool chained_async_continuation_called = false;
static bool first_parallel_async_called = false;
static bool second_parallel_async_called = false;

typedef struct TestServiceMappedMemoryRequest
{
    uint64_t physical_address;
    uint64_t size;
    uint64_t driver_address;
    uint64_t device_address;
}
TestServiceMappedMemoryRequest;

static void test_resume_process(ProcessPtr process)
{
    ContextSwitch__switch(ContextSwitch__new(process, None(ProcessPtr)));
}

void Arch__reset_irq_tracking();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();
uint64_t Arch__enabled_irq();
uint64_t Arch__enable_count();

bool Fat32FilesystemIO__available()
{
    return true;
}

FileIO Fat32FilesystemIO__new()
{
    return MemFilesystemIO__new();
}

void * Fat32FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return MemFilesystemIOState__heap_new(fs);
}

bool Ext2FilesystemIO__available()
{
    return true;
}

FileIO Ext2FilesystemIO__new()
{
    return MemFilesystemIO__new();
}

void * Ext2FilesystemIOState__heap_new(FilesystemPtr fs, BlockDevicePtr device, uint64_t total_sectors)
{
    return MemFilesystemIOState__heap_new(fs);
}

void setUp(void)
{
    clear_panic_state();
    clear_deferred_syscall_result();
    current_process_reset();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel_without_filesystem();
    ResultMatch(SchedulerRegisterCode, Scheduler__register(kernel))
        IsError(code, message, TEST_FAIL())
    End
    async_continuation_called = false;
    async_continuation_blocks = 0;
    async_continuation_has_buffer = false;
    async_write_continuation_called = false;
    chained_async_continuation_called = false;
    first_parallel_async_called = false;
    second_parallel_async_called = false;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_service_handler_wait_copies_queued_event(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    ServiceEvent queued = (ServiceEvent) {
        .type = SERVICE_EVENT_IRQ,
        .source = 44,
        .data = 99,
    };
    ServiceEvent target = (ServiceEvent) { 0 };

    ResultMatch(ServiceCode, Scheduler__service_enqueue(kernel->scheduler, process, queued))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
            TEST_ASSERT_EQUAL(44, target.source);
            TEST_ASSERT_EQUAL(99, target.data);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_handler_become_service_marks_process_as_service(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_NORMAL;

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_BECOME_SERVICE, 0, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROCESS_CLASS_SERVICE, process->process_class);
}

void test_service_handler_become_service_denies_unprivileged_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_NORMAL;
    Process__set_identity(process, 1000, 1000, PROCESS_CAP_NONE);

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_BECOME_SERVICE, 0, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code))
    End

    TEST_ASSERT_EQUAL(PROCESS_CLASS_NORMAL, process->process_class);
}

void test_service_handler_map_mmio_denies_unprivileged_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    Process__set_identity(process, 1000, 1000, PROCESS_CAP_NONE);
    TestServiceMappedMemoryRequest request = {
        .physical_address = 0x09000000,
        .size = 0x1000,
        .driver_address = 0,
        .device_address = 0,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_MAP_MMIO, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code))
    End
}

void test_service_handler_translates_service_user_address_to_device_address(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    Result(UserAddress) allocation_result = Process__alloc_user_pages(process, 512);
    TEST_ASSERT_TRUE(allocation_result.ok);
    UserAddress allocation = ResultUnwrap(UserAddress, allocation_result, NULL_USER_ADDRESS);
    ServiceDeviceAddressRequest request = {
        .driver_address = allocation.virtual_addr + 128,
        .device_address = 0,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_DEVICE_ADDRESS, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL_UINT64(allocation.kernel_addr.phys_addr + 128, request.device_address);
        })
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_handler_wait_parks_and_completes_when_event_arrives(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceEvent target = (ServiceEvent) { 0 };

    Scheduler__schedule(kernel->scheduler, process);

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);

    ServiceEvent queued = (ServiceEvent) {
        .type = SERVICE_EVENT_MESSAGE,
        .source = 7,
        .data = 11,
    };

    ResultMatch(ServiceCode, Scheduler__service_enqueue(kernel->scheduler, process, queued))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->state);

    test_resume_process(process);

    TEST_ASSERT_EQUAL(SERVICE_OK, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));

    TEST_ASSERT_EQUAL(SERVICE_EVENT_MESSAGE, target.type);
    TEST_ASSERT_EQUAL(7, target.source);
    TEST_ASSERT_EQUAL(11, target.data);
    TEST_ASSERT_EQUAL(0, ServiceEvents__len(&process->service.events));
}

void test_service_handler_wait_completes_previous_irq_before_next_wait(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;
    ServiceEvent target = (ServiceEvent) { 0 };

    Scheduler__schedule(kernel->scheduler, process);

    ResultMatch(ServiceCode, Scheduler__subscribe_irq(kernel->scheduler, process, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ServiceCode, Scheduler__send_irq_event(kernel->scheduler, irq))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
    TEST_ASSERT_EQUAL(irq, target.source);
    TEST_ASSERT_TRUE(Process__is_handling_service_irq(process));
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &target, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code))
    End

    TEST_ASSERT_EQUAL(1, Arch__unmask_count());
    TEST_ASSERT_FALSE(Process__is_handling_service_irq(process));
}

void test_service_handler_subscribe_irq_registers_service_consumer(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 48;

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL_UINT64(irq, Arch__enabled_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());

    IRQDispatchContext context = (IRQDispatchContext) {
        .irq = irq,
        .cpu = cpu,
        .current_el = false,
        .process_context = NULL,
    };

    ResultMatch(IRQDispatchStatus, IRQDispatcher__dispatch(kernel->irq_dispatcher, irq, &context))
        IsSuccess(status, TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status))
        IsError(code, message, TEST_FAIL())
    End

    ServiceEvent event = (ServiceEvent) { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, event.type);
    TEST_ASSERT_EQUAL_UINT64(irq, event.source);
}

void test_service_handler_subscribe_irq_maps_duplicate_registration_to_service_code(void)
{
    ProcessPtr first_process = dummy_process(1, cpu, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, cpu, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 48;

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, first_process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, second_process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_IRQ_ALREADY_REGISTERED, code))
    End

    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());
}

void test_service_handler_registers_block_device_for_service_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = {
        .path = "/dev/virtio1",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    BString path = bstring("/dev/virtio1");
    Match(BlockDevicePtr, BlockDeviceRegistry__find(path))
        IsSome(device, TEST_ASSERT_NOT_NULL(device))
        IsNone(TEST_FAIL())
    End
}

void test_service_handler_registering_block_device_mounts_matching_filesystem_table_entry(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    ResultMatch(CollectionCode, FilesystemTable__add(
        &root->filesystem_table,
        bstring("/dev/virtio-table"),
        bstring("fat32"),
        bstring("/mnt/fs1")
    ))
        IsSuccess(index, {})
        IsError(code, message, TEST_FAIL())
    End
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);

    Match(FilesystemPtr, Mounts__get(&root->mounts, bstring("/mnt/fs1")))
        IsSome(mounted, TEST_FAIL())
        IsNone({})
    End

    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = {
        .path = "/dev/virtio-table",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Match(FilesystemPtr, Mounts__get(&root->mounts, bstring("/mnt/fs1")))
        IsSome(mounted, TEST_ASSERT_NOT_NULL(mounted))
        IsNone(TEST_FAIL())
    End
}

void test_service_handler_registering_block_device_mounts_root_filesystem_table_entry(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    ResultMatch(CollectionCode, FilesystemTable__add(
        &root->filesystem_table,
        bstring("/dev/virtio-root"),
        bstring("ext2"),
        bstring("/")
    ))
        IsSuccess(index, {})
        IsError(code, message, TEST_FAIL())
    End
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);

    Match(FilesystemPtr, Mounts__get(&root->mounts, bstring("/")))
        IsSome(mounted, TEST_FAIL())
        IsNone({})
    End

    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = {
        .path = "/dev/virtio-root",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Match(FilesystemPtr, Mounts__get(&root->mounts, bstring("/")))
        IsSome(mounted, TEST_ASSERT_NOT_NULL(mounted))
        IsNone(TEST_FAIL())
    End
}

void test_service_handler_rejects_duplicate_block_device_registration(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = {
        .path = "/dev/virtio-duplicate",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code))
    End
}

void test_service_handler_process_cleanup_unregisters_block_device(void)
{
    ProcessPtr first_process = dummy_process(1, cpu, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, cpu, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = {
        .path = "/dev/virtio-cleanup",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, first_process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-cleanup")))
        IsSome(device, TEST_ASSERT_NOT_NULL(device))
        IsNone(TEST_FAIL())
    End

    ResultMatch(ProcessSetupStatus, Process__dispose_owned_kernel_resources(kernel, first_process))
        IsSuccess(status, TEST_ASSERT_EQUAL(PROCESS_SETUP, status))
        IsError(code, message, TEST_FAIL())
    End

    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-cleanup")))
        IsSome(device, TEST_FAIL())
        IsNone({})
    End

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, second_process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &request, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_handler_gets_and_completes_block_write_request(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-protocol",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-protocol")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    Data source[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    source[0] = 42;
    ResultMatch(BlockCount, BlockDevice__write(device, 7, 1, source))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End

    ServiceEvent event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_IO, event.type);
    TEST_ASSERT_NOT_EQUAL(0, event.source);

    ServiceBlockRequest request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything) &request, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(event.source, request.request_id);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio-protocol", request.device_path);
    TEST_ASSERT_EQUAL(SERVICE_BLOCK_WRITE, request.operation);
    TEST_ASSERT_EQUAL_UINT64(7, request.block);
    TEST_ASSERT_EQUAL_UINT64(1, request.block_count);
    TEST_ASSERT_EQUAL_UINT64(BLOCK_DEVICE_BLOCK_SIZE, request.block_size);
    TEST_ASSERT_EQUAL_UINT64(BLOCK_DEVICE_BLOCK_SIZE, request.buffer_size);
    TEST_ASSERT_NOT_NULL(request.buffer);

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, process), SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End
}

void test_service_handler_block_completion_unparks_waiting_reader(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-park",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, reader))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-park")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    KernelContext context = KernelContext__new(Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler));
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    BlockDevice__set_current_context(&context);
    ResultMatch(BlockCount, BlockDevice__read(device, 9, 1, target))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything) &request, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
}

static Result(Anything) async_read_continuation(BlockIOContextPtr context)
{
    async_continuation_called = true;
    async_continuation_blocks = context->completed_blocks;
    async_continuation_has_buffer = context->buffer != NULL && context->buffer_size == BLOCK_DEVICE_BLOCK_SIZE;
    return Success(Anything, 77);
}

static Result(Anything) async_write_continuation(BlockIOContextPtr context)
{
    async_write_continuation_called = true;
    async_continuation_blocks = context->completed_blocks;
    return Success(Anything, 88);
}

static Result(Anything) chained_async_read_continuation(BlockIOContextPtr context)
{
    if (context->block == 9)
    {
        BlockDevicePtr device = (BlockDevicePtr) context->owner_context;
        BlockIOContextPtr next_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
        next_context->kernel_context = context->kernel_context;
        next_context->owner_context = context->owner_context;

        return BlockDevice__read_async(device, 10, 1, next_context, chained_async_read_continuation);
    }

    chained_async_continuation_called = true;
    async_continuation_blocks = context->completed_blocks;
    return Success(Anything, 99);
}

static Result(Anything) first_parallel_async_continuation(BlockIOContextPtr context)
{
    first_parallel_async_called = true;
    async_continuation_blocks = context->completed_blocks;
    return Success(Anything, 101);
}

static Result(Anything) second_parallel_async_continuation(BlockIOContextPtr context)
{
    second_parallel_async_called = true;
    async_continuation_blocks = context->completed_blocks;
    return Success(Anything, 202);
}

void test_service_handler_block_completion_runs_async_read_continuation_with_kernel_buffer(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-async",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, reader))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-async")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = KernelContext__new(
        Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    block_context->owner_context = 0;

    BlockDevice__set_current_context(&block_context->kernel_context);
    ResultMatch(Anything, BlockDevice__read_async(device, 9, 1, block_context, async_read_continuation))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything) &request, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);

    test_resume_process(reader);

    TEST_ASSERT_TRUE(async_continuation_called);
    TEST_ASSERT_TRUE(async_continuation_has_buffer);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(77, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));
}

void test_service_handler_chained_async_read_parks_from_block_context(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-chain",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, reader))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-chain")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = KernelContext__new(
        Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    block_context->owner_context = (Anything) device;

    BlockDevice__set_current_context(&block_context->kernel_context);
    ResultMatch(Anything, BlockDevice__read_async(device, 9, 1, block_context, chained_async_read_continuation))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent first_event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &first_event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest first_request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, first_event.source, (Anything) &first_request, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(9, first_request.block);
        })
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion first_completion = {
        .request_id = first_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &first_completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent second_event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &second_event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest second_request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, second_event.source, (Anything) &second_request, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(10, second_request.block);
        })
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion second_completion = {
        .request_id = second_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &second_completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_TRUE(chained_async_continuation_called);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(99, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));
}

void test_service_handler_parallel_async_reads_complete_out_of_order_without_resuming_unready_request(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-parallel",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, reader))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-parallel")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    BlockIOContextPtr first_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    first_context->kernel_context = KernelContext__new(
        Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    first_context->owner_context = 0;

    BlockIOContextPtr second_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    second_context->kernel_context = KernelContext__new(
        Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    second_context->owner_context = 0;

    BlockDevice__set_current_context(&first_context->kernel_context);
    ResultMatch(Anything, BlockDevice__read_async(device, 9, 1, first_context, first_parallel_async_continuation))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    ResultMatch(Anything, BlockDevice__read_async(device, 10, 1, second_context, second_parallel_async_continuation))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent first_event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &first_event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceEvent second_event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &second_event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest first_request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, first_event.source, (Anything) &first_request, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest second_request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, second_event.source, (Anything) &second_request, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(9, first_request.block);
    TEST_ASSERT_EQUAL(10, second_request.block);

    ServiceBlockCompletion second_completion = {
        .request_id = second_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &second_completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_FALSE(first_parallel_async_called);
    TEST_ASSERT_TRUE(second_parallel_async_called);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    TEST_ASSERT_TRUE(Process__is_parked(reader));
    TEST_ASSERT_EQUAL_UINT64(1, ParkedOperations__len(&reader->parked_operations));
    TEST_ASSERT_EQUAL(202, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));

    ServiceBlockCompletion first_completion = {
        .request_id = first_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &first_completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_TRUE(first_parallel_async_called);
    TEST_ASSERT_FALSE(Process__is_parked(reader));
    TEST_ASSERT_EQUAL_UINT64(0, ParkedOperations__len(&reader->parked_operations));
    TEST_ASSERT_EQUAL(101, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));
}

void test_service_handler_failed_block_completion_unparks_waiting_reader_with_error(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-park-fail",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, reader))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-park-fail")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    KernelContext context = KernelContext__new(Some(ProcessID, reader->pid), Some(SchedulerPtr, kernel->scheduler));
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    BlockDevice__set_current_context(&context);
    ResultMatch(BlockCount, BlockDevice__read(device, 9, 1, target))
        IsSuccess(count, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion completion = {
        .request_id = event.source,
        .status = SERVICE_BLOCK_REQUEST_FAILED,
        .block_count = 0,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);

    test_resume_process(reader);
    TEST_ASSERT_EQUAL(BLOCK_DEVICE_IO_ERROR, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));
}

void test_service_handler_block_completion_runs_async_write_continuation(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = {
        .path = "/dev/virtio-async-write",
        .block_size = BLOCK_DEVICE_BLOCK_SIZE,
        .block_count = 128,
    };

    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything) &registration, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ProcessPtr writer = dummy_process(2, cpu, PROC_STATE_READY);
    ResultMatch(CPUID, Scheduler__schedule(kernel->scheduler, writer))
        IsSuccess(scheduled_cpu, {})
        IsError(code, message, TEST_FAIL())
    End

    BlockDevicePtr device = NULL;
    Match(BlockDevicePtr, BlockDeviceRegistry__find(bstring("/dev/virtio-async-write")))
        IsSome(found, device = found)
        IsNone(TEST_FAIL())
    End

    Data source[BLOCK_DEVICE_BLOCK_SIZE] = { 0 };
    source[0] = 0x5a;
    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = KernelContext__new(
        Some(ProcessID, writer->pid), Some(SchedulerPtr, kernel->scheduler)
    );
    block_context->owner_context = 0;

    BlockDevice__set_current_context(&block_context->kernel_context);
    ResultMatch(Anything, BlockDevice__write_async(device, 12, 1, source, block_context, async_write_continuation))
        IsSuccess(result, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code))
    End
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, writer->state);

    ServiceEvent event = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything) &event, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockRequest request = { 0 };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything) &request, 0, 0
    ))
        IsSuccess(code,
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_BLOCK_WRITE, request.operation);
            TEST_ASSERT_EQUAL(12, request.block);
            TEST_ASSERT_EQUAL(1, request.block_count);
        })
        IsError(code, message, TEST_FAIL())
    End

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    ResultMatch(SystemCallResult, Service__handler(
        kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything) &completion, 0, 0, 0
    ))
        IsSuccess(code, TEST_ASSERT_EQUAL(SERVICE_OK, code))
        IsError(code, message, TEST_FAIL())
    End

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, writer->state);

    test_resume_process(writer);

    TEST_ASSERT_TRUE(async_write_continuation_called);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(88, ResultUnwrap(Anything, deferred_syscall_result(), (Anything) -999));
}

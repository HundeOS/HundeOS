#include "unity.h"
#include <string.h>

#include "../../../kernel/syscall/service_handler.h"
#include "../../../kernel/syscall/io_handler.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/service/service_directory.h"
#include "../../../kernel/scheduler/context_switch.h"
#include "../../../kernel/scheduler/register.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/kalloc.h"
#include "../../../kernel/subsystems/io/block/block_device.h"
#include "../../../kernel/subsystems/io/register.h"
#include "../../../test/kernel/fixtures/memfs/memfs.h"
#include "../../../kernel/subsystems/io/socket/socket.h"
#include "../../../common_includes/syscalls/service.h"
#include "../../../common_includes/syscalls/socket.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"

#define Service__handler(kernel, process, ...) Service__handler((kernel), &(process), __VA_ARGS__)
#define IO__handler(kernel, process, ...) IO__handler((kernel), &(process), __VA_ARGS__)

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;
static bool async_continuation_called = false;
static BlockCount async_continuation_blocks = 0;
static bool async_continuation_has_buffer = false;
static bool async_write_continuation_called = false;
static bool chained_async_continuation_called = false;
static bool first_parallel_async_called = false;
static bool second_parallel_async_called = false;

static Optional(BlockDevicePtr) TestBlockDeviceRegistry__find(BString path)
{
    return BlockDeviceRegistry__find(&path);
}

typedef struct TestServiceMappedMemoryRequest
{
    Address physical_address;
    size_t size;
    Address driver_address;
    Address device_address;
} TestServiceMappedMemoryRequest;

static void test_resume_process(ProcessPtr process)
{
    ContextSwitch context_switch = ContextSwitch__new(process, &None(ProcessPtr));
    ContextSwitch__switch(&context_switch);
}

static KernelContext test_kernel_context_for_process(ProcessPtr process)
{
    return KernelContext__new(&Some(ProcessID, process->pid), &Some(SchedulerPtr, kernel->scheduler));
}

static ServiceReadyRequest test_ready_request(const char * name)
{
    BString ready_name = bstring(name);
    return ServiceReadyRequest__new(&ready_name);
}

static ServiceBlockDeviceRegistration test_block_registration(const char * path, BlockCount block_count)
{
    BString device_path = bstring(path);
    return ServiceBlockDeviceRegistration__new(&device_path, BLOCK_DEVICE_BLOCK_SIZE, block_count);
}

static ServiceFilesystemRegistration test_filesystem_registration(const char * device_path,
                                                                  const char * filesystem_type, const char * mountpoint)
{
    BString device = bstring(device_path);
    BString type = bstring(filesystem_type);
    BString mount = bstring(mountpoint);
    return ServiceFilesystemRegistration__new(&device, &type, &mount);
}

static IOListDirRequest test_list_dir_request(const char * path, DirectoryEntryPtr entries, size_t max_entries)
{
    BString list_path = bstring(path);
    return IOListDirRequest__new(&list_path, entries, max_entries);
}

static Result(Anything) test_socket_complete_identity(IOSocketRouterRequestPtr request,
                                                      IOSocketRouterCompletionPtr completion, Anything context)
{
    if (completion->status != IO_SOCKET_OK)
    {
        return Error(Anything, bstr("socket failed"), completion->status);
    }

    return Success(Anything, completion->result);
}

static IOSocketRouterRequest test_socket_router_take_request(ProcessPtr router)
{
    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_SOCKET_IO, event.type);

    IOSocketRouterRequest request = IOSocketRouterRequest__new(0, IO_SOCKET_ROUTER_LISTEN, &bstr(""), 0, 0);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_GET_REQUEST, ServiceEvent__request_id(&event),
                         (Anything)&request, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    return request;
}

static SocketRegistryPtr test_socket_registry(void)
{
    match_result(
        SocketIO__registered_registry(kernel->io_subsystem),
        success(registry) { return registry; },
        error_test_fail()
    );
}

void Arch__reset_irq_tracking();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();
uint64_t Arch__enabled_irq();
uint64_t Arch__enable_count();

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
    match_result(Scheduler__register(kernel),
        success_do_nothing(),
        error_test_fail()
    );
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

    ServiceEvent queued = (ServiceEvent){
        .type = SERVICE_EVENT_IRQ,
        .source = 44,
        .data = 99,
    };
    ServiceEvent target = (ServiceEvent){0};

    match_result(
        Scheduler__service_enqueue(kernel->scheduler, process, &queued),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
            TEST_ASSERT_EQUAL(44, target.source);
            TEST_ASSERT_EQUAL(99, target.data);
        },
        error_test_fail()
    );
}

void test_service_handler_become_service_marks_process_as_service(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_NORMAL;

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_BECOME_SERVICE, 0, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROCESS_CLASS_SERVICE, process->process_class);
}

void test_service_handler_become_service_denies_unprivileged_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_NORMAL;
    Process__set_identity(process, 1000, 1000, PROCESS_CAP_NONE);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_BECOME_SERVICE, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code); }
    );

    TEST_ASSERT_EQUAL(PROCESS_CLASS_NORMAL, process->process_class);
}

void test_service_handler_authorizes_only_live_capable_block_clients(void)
{
    ProcessPtr service = dummy_process(101, cpu, PROC_STATE_READY);
    ProcessPtr client = dummy_process(102, cpu, PROC_STATE_READY);
    Process__set_hund_identity(service);
    Process__set_hund_identity(client);
    Scheduler__schedule_on_cpu(kernel->scheduler, client, cpu);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, client->pid, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    Process__set_identity(client, 1000, 1000, PROCESS_CAP_NONE);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code); }
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, 99999, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code); }
    );
    Process__set_identity(service, 1000, 1000, PROCESS_CAP_NONE);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code); }
    );
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

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_MAP_MMIO, (Anything)&request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code); }
    );
}

void test_service_handler_translates_service_user_address_to_device_address(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;

    Result(UserAddress) allocation_result = Process__alloc_user_pages(process, 512);
    TEST_ASSERT_TRUE(allocation_result.ok);
    UserAddress allocation = unwrap_result(allocation_result, NULL_USER_ADDRESS);
    ServiceDeviceAddressRequest request = ServiceDeviceAddressRequest__new(allocation.virtual_addr + 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_DEVICE_ADDRESS, (Anything)&request, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL_UINT64(allocation.kernel_addr.phys_addr + 128, request.device_address);
        },
        error_test_fail()
    );
}

void test_service_handler_wait_parks_and_completes_when_event_arrives(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceEvent target = (ServiceEvent){0};

    Scheduler__schedule(kernel->scheduler, process);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code); }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);

    ServiceEvent queued = (ServiceEvent){
        .type = SERVICE_EVENT_MESSAGE,
        .source = 7,
        .data = 11,
    };

    match_result(
        Scheduler__service_enqueue(kernel->scheduler, process, &queued),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->state);

    test_resume_process(process);

    TEST_ASSERT_EQUAL(SERVICE_OK, unwrap_result(deferred_syscall_result(), (Anything)-999));

    TEST_ASSERT_EQUAL(SERVICE_EVENT_MESSAGE, target.type);
    TEST_ASSERT_EQUAL(7, target.source);
    TEST_ASSERT_EQUAL(11, target.data);
    TEST_ASSERT_EQUAL(0, ServiceEvents__len(&process->service.events));
}

void test_service_handler_wait_ready_returns_immediately_when_name_was_published(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceReadyRequest request = test_ready_request("/dev/mmc0");

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_PUBLISH_READY, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT_READY, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
}

void test_service_handler_publish_ready_unparks_waiter(void)
{
    ProcessPtr waiter = dummy_process(1, cpu, PROC_STATE_READY);
    waiter->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr publisher = dummy_process(2, cpu, PROC_STATE_READY);
    publisher->process_class = PROCESS_CLASS_SERVICE;
    ServiceReadyRequest request = test_ready_request("/dev/mmc0");

    Scheduler__schedule(kernel->scheduler, waiter);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, waiter), SERVICE_WAIT_READY, (Anything)&request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code); }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, waiter->state);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, publisher), SERVICE_PUBLISH_READY, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, waiter->state);

    test_resume_process(waiter);

    TEST_ASSERT_EQUAL(SERVICE_OK, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_publish_ready_does_not_register_endpoint(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceReadyRequest ready = test_ready_request("hundekanal-warter");

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_PUBLISH_READY, (Anything)&ready, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(&kernel->service_directory);
    TEST_ASSERT_EQUAL_UINT64(0, directory->entry_count);
}

void test_service_handler_wait_completes_previous_irq_before_next_wait(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 33;
    ServiceEvent target = (ServiceEvent){0};

    Scheduler__schedule(kernel->scheduler, process);

    match_result(
        Scheduler__subscribe_irq(kernel->scheduler, process, irq),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(kernel->scheduler, irq),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
    TEST_ASSERT_EQUAL(irq, target.source);
    TEST_ASSERT_TRUE(Process__is_handling_service_irq(process));
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code); }
    );

    TEST_ASSERT_EQUAL(1, Arch__unmask_count());
    TEST_ASSERT_FALSE(Process__is_handling_service_irq(process));
}

void test_service_handler_subscribe_irq_registers_service_consumer(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    IRQNumber irq = 48;

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(irq, Arch__enabled_irq());
    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());

    IRQDispatchContext context = (IRQDispatchContext){
        .irq = irq,
        .cpu = cpu,
        .current_el = false,
        .process_context = NULL,
    };

    match_result(
        IRQDispatcher__dispatch(kernel->irq_dispatcher, irq, &context),
        success(status) { TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status); },
        error_test_fail()
    );

    ServiceEvent event = (ServiceEvent){0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

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

    match_result(
        Service__handler(kernel, Some(ProcessPtr, first_process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, second_process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_IRQ_ALREADY_REGISTERED, code); }
    );

    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());
}

void test_service_handler_registers_block_device_for_service_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = test_block_registration("/dev/virtio1", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    BString path = bstring("/dev/virtio1");
    match_optional(
        TestBlockDeviceRegistry__find(path),
        some(device) { TEST_ASSERT_NOT_NULL(device); },
        none_test_fail()
    );
}

void test_service_handler_registering_block_device_mounts_root_filesystem_table_entry(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    BString device_path = bstring("/dev/virtio-root");
    match_result(FilesystemTable__add(&root->filesystem_table, &device_path, &bstr("bootstrap-ext2"), &bstr("/")),
                 success_do_nothing(),
        error_test_fail()
    );
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);

    match_optional(Mounts__get(&root->mounts, bstring("/")),
        some_test_fail(),
        none_do_nothing()
    );

    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = test_block_registration("/dev/virtio-root", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_optional(
        Mounts__get(&root->mounts, bstring("/")),
        some(mounted) { TEST_ASSERT_NOT_NULL(mounted); },
        none_test_fail()
    );
}

void test_service_handler_rejects_duplicate_block_device_registration(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = test_block_registration("/dev/virtio-duplicate", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code); }
    );
}

void test_service_handler_block_connect_maps_and_notifies_block_service(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/channel-blk", 8);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr filesystem = dummy_process(2, cpu, PROC_STATE_READY);
    BString device_path = bstring("/dev/channel-blk");
    ServiceBlockConnectionRequest open_request = ServiceBlockConnectionRequest__new(&device_path);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem), SERVICE_BLOCK_CONNECT, (Anything)&open_request, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_NOT_EQUAL_UINT64(0, open_request.connection_id);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, open_request.shared_memory);
    TEST_ASSERT_EQUAL_UINT64(sizeof(BlockProtocolMemory), open_request.shared_memory_size);

    ServiceEvent event = ServiceEvent__request(SERVICE_EVENT_MESSAGE, 0, 0);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_CONNECTION_OPENED, event.type);
    TEST_ASSERT_EQUAL_UINT64(open_request.connection_id, event.source);
    TEST_ASSERT_NOT_EQUAL_UINT64(0, event.data);

    ServiceBlockDisconnectionRequest close_request = ServiceBlockDisconnectionRequest__new(open_request.connection_id);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem), SERVICE_BLOCK_DISCONNECT, (Anything)&close_request, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    event = ServiceEvent__request(SERVICE_EVENT_MESSAGE, 0, 0);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_CONNECTION_CLOSED, event.type);
    TEST_ASSERT_EQUAL_UINT64(open_request.connection_id, event.source);
}

void test_service_handler_process_cleanup_unregisters_block_device(void)
{
    BlockDevicePtr retained;
    ProcessPtr first_process = dummy_process(1, cpu, PROC_STATE_READY);
    first_process->process_class = PROCESS_CLASS_SERVICE;
    ProcessPtr second_process = dummy_process(2, cpu, PROC_STATE_READY);
    second_process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = test_block_registration("/dev/virtio-cleanup", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, first_process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-cleanup")),
        some(device)
        {
            retained = device;
            BlockDevice__retain(retained);
        },
        none_test_fail()
    );

    match_result(
        Process__dispose_owned_kernel_resources(first_process),
        success(status) { TEST_ASSERT_EQUAL(PROCESS_SETUP, status); },
        error_test_fail()
    );

    match_optional(TestBlockDeviceRegistry__find(bstring("/dev/virtio-cleanup")),
        some_test_fail(),
        none_do_nothing()
    );

    Data sector[512];
    match_result(
        BlockDevice__read(retained, 0, 1, sector),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_IO_ERROR, code); }
    );
    BlockDevice__release(retained);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, second_process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
}

void test_service_handler_gets_and_completes_block_write_request(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-protocol", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-protocol")),
        some(found) { device = found; },
        none_test_fail()
    );

    Data source[BLOCK_DEVICE_BLOCK_SIZE] = {0};
    source[0] = 42;
    match_result(
        BlockDevice__write(device, 7, 1, source),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_IO, event.type);
    TEST_ASSERT_NOT_EQUAL(0, event.source);

    ServiceBlockRequest request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything)&request,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(event.source, request.request_id);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio-protocol"), request.device_path);
    TEST_ASSERT_EQUAL(SERVICE_BLOCK_WRITE, request.operation);
    TEST_ASSERT_EQUAL_UINT64(7, request.block);
    TEST_ASSERT_EQUAL_UINT64(1, request.block_count);
    TEST_ASSERT_EQUAL_UINT64(BLOCK_DEVICE_BLOCK_SIZE, request.block_size);
    TEST_ASSERT_EQUAL_UINT64(BLOCK_DEVICE_BLOCK_SIZE, request.buffer_size);
    TEST_ASSERT_NOT_EQUAL(0, request.buffer);

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
}

void test_service_handler_block_completion_unparks_waiting_reader(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-park", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-park")),
        some(found) { device = found; },
        none_test_fail()
    );

    KernelContext context = test_kernel_context_for_process(reader);
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = {0};
    BlockDevice__set_current_context(&context);
    match_result(
        BlockDevice__read(device, 9, 1, target),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything)&request,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
}

void test_service_handler_block_read_device_maps_requester_buffer_into_block_service(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-shared-read", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    UserAddress reader_buffer =
        unwrap_result(Process__alloc_user_pages(reader, BLOCK_DEVICE_BLOCK_SIZE), NULL_USER_ADDRESS);
    BString read_path = bstring("/dev/virtio-shared-read");
    ServiceBlockReadRequest read_request =
        ServiceBlockReadRequest__new(&read_path, 13, 1, reader_buffer.virtual_addr, BLOCK_DEVICE_BLOCK_SIZE);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, reader), SERVICE_BLOCK_READ_DEVICE, (Anything)&read_request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest block_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source,
                         (Anything)&block_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_BLOCK_READ, block_request.operation);
            TEST_ASSERT_EQUAL(13, block_request.block);
            TEST_ASSERT_EQUAL(1, block_request.block_count);
            TEST_ASSERT_NOT_EQUAL(0, block_request.buffer);
        },
        error_test_fail()
    );

    match_result(
        Process__device_address(reader, (Address)read_request.buffer),
        success(reader_phys)
        {
            match_result(
                Process__device_address(service, (Address)block_request.buffer),
                success(service_phys) { TEST_ASSERT_EQUAL(reader_phys, service_phys); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    ServiceBlockCompletion completion = {
        .request_id = block_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);

    test_resume_process(reader);
    TEST_ASSERT_EQUAL(1, unwrap_result(deferred_syscall_result(), (Anything)-999));

    match_result(
        Process__device_address(service, (Address)block_request.buffer),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_NOT_FOUND, code); }
    );
}

void test_service_handler_async_block_read_returns_request_id_and_response_event(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-async-user", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr requester = dummy_process(2, cpu, PROC_STATE_READY);
    requester->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule(kernel->scheduler, requester),
        success_do_nothing(),
        error_test_fail()
    );

    UserAddress requester_buffer =
        unwrap_result(Process__alloc_user_pages(requester, BLOCK_DEVICE_BLOCK_SIZE), NULL_USER_ADDRESS);
    ServiceBlockReadRequest read_request = ServiceBlockReadRequest__new(
        &bstr("/dev/virtio-async-user"), 21, 1, requester_buffer.virtual_addr, BLOCK_DEVICE_BLOCK_SIZE);

    uint64_t request_id = 0;
    match_result(
        Service__handler(kernel, Some(ProcessPtr, requester), SERVICE_BLOCK_READ_DEVICE_ASYNC, (Anything)&read_request,
                         0, 0, 0),
        success(value) { request_id = value; },
        error_test_fail()
    );

    TEST_ASSERT_NOT_EQUAL(0, request_id);
    TEST_ASSERT_EQUAL(PROC_STATE_READY, requester->state);

    ServiceEvent request_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&request_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_REQUEST, ServiceEvent__kind(&request_event));
    TEST_ASSERT_EQUAL(request_id, ServiceEvent__request_id(&request_event));

    ServiceBlockRequest block_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, request_id,
                         (Anything)&block_request, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_BLOCK_READ, block_request.operation);
    TEST_ASSERT_EQUAL(21, block_request.block);

    ServiceBlockCompletion completion = ServiceBlockCompletion__new(request_id, SERVICE_OK, 1);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceEvent response_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, requester), SERVICE_WAIT, (Anything)&response_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_RESPONSE, ServiceEvent__kind(&response_event));
    TEST_ASSERT_EQUAL(SERVICE_EVENT_BLOCK_IO, response_event.type);
    TEST_ASSERT_EQUAL(request_id, ServiceEvent__request_id(&response_event));
    TEST_ASSERT_EQUAL(1, response_event.data);
}

void test_service_handler_socket_router_gets_and_completes_request(void)
{
    match_result(IOSubsystem__register(kernel),
        success_do_nothing(),
        error_test_fail()
    );
    SocketRegistryPtr registry = test_socket_registry();
    SocketRegistry__reset(registry);

    ProcessPtr router = dummy_process(1, cpu, PROC_STATE_READY);
    router->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule(kernel->scheduler, router),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_REGISTER_SOCKET_ROUTER, 0, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr requester = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, requester),
        success_do_nothing(),
        error_test_fail()
    );

    BString uri = bstring("socket://proc/sock1");
    IOSocketRouterRequest request = IOSocketRouterRequest__new(0, IO_SOCKET_ROUTER_LISTEN, &uri, 42, 4);
    KernelContext kernel_context = test_kernel_context_for_process(requester);

    match_result(
        SocketRegistry__start_request(registry, request, kernel_context, test_socket_complete_identity, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, requester->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_SOCKET_IO, event.type);
    TEST_ASSERT_EQUAL(SERVICE_EVENT_REQUEST, ServiceEvent__kind(&event));

    IOSocketRouterRequest fetched_request = IOSocketRouterRequest__new(0, IO_SOCKET_ROUTER_LISTEN, &bstr(""), 0, 0);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_GET_REQUEST, ServiceEvent__request_id(&event),
                         (Anything)&fetched_request, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(ServiceEvent__request_id(&event), fetched_request.request_id);
    TEST_ASSERT_EQUAL(IO_SOCKET_ROUTER_LISTEN, fetched_request.operation);
    TEST_ASSERT_EQUAL_INT64(42, fetched_request.handle);
    TEST_ASSERT_EQUAL_UINT64(4, fetched_request.backlog);
    TEST_ASSERT_EQUAL_BSTRING(bstr("socket://proc/sock1"), fetched_request.uri);

    IOSocketRouterCompletion completion = IOSocketRouterCompletion__new(fetched_request.request_id, IO_SOCKET_OK, 42);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, requester->state);
    test_resume_process(requester);
    TEST_ASSERT_EQUAL(42, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_socket_open_accept_write_and_read(void)
{
    match_result(IOSubsystem__register(kernel),
        success_do_nothing(),
        error_test_fail()
    );
    SocketRegistry__reset(test_socket_registry());

    ProcessPtr router = dummy_process(1, cpu, PROC_STATE_READY);
    router->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule(kernel->scheduler, router),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_REGISTER_SOCKET_ROUTER, 0, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr process = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, process),
        success_do_nothing(),
        error_test_fail()
    );
    BString uri = bstring("socket://proc/sock1");

    IOPathRequest listen_path = IOPathRequest__new(&bstr("socket://proc/waiting"), IO_OPEN_BIND | IO_OPEN_LISTEN);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, process), IO_OPEN, (Anything)&listen_path, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    IOSocketRouterRequest listen_request = test_socket_router_take_request(router);
    TEST_ASSERT_EQUAL(IO_SOCKET_ROUTER_LISTEN, listen_request.operation);
    IOSocketRouterCompletion listen_completion =
        IOSocketRouterCompletion__new(listen_request.request_id, IO_SOCKET_OK, listen_request.handle);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_COMPLETE, (Anything)&listen_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    test_resume_process(process);
    FileDescriptor listener_fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);

    IOPathRequest connect_path = IOPathRequest__new(&bstr("socket://proc/waiting"), IO_OPEN_READ | IO_OPEN_WRITE);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, process), IO_OPEN, (Anything)&connect_path, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    IOSocketRouterRequest connect_request = test_socket_router_take_request(router);
    TEST_ASSERT_EQUAL(IO_SOCKET_ROUTER_CONNECT, connect_request.operation);
    IOSocketRouterCompletion connect_completion =
        IOSocketRouterCompletion__new(connect_request.request_id, IO_SOCKET_OK, listen_request.handle);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_COMPLETE, (Anything)&connect_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    test_resume_process(process);
    FileDescriptor client_fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);

    match_result(
        IO__handler(kernel, Some(ProcessPtr, process), IO_ACCEPT, listener_fd, 0, 0, 0),
        success(accepted_fd)
        {
            BString text = bstring("ping");
            match_result(
                IO__handler(kernel, Some(ProcessPtr, process), IO_WRITE, client_fd,
                            (Anything)BString__cstr(&bstr("ping")), BString__length(&bstr("ping")), 0),
                success(bytes) { TEST_ASSERT_EQUAL(4, bytes); },
                error_test_fail()
            );

            char read_buffer[8] = {0};
            match_result(
                IO__handler(kernel, Some(ProcessPtr, process), IO_READ, accepted_fd, (Anything)read_buffer, 4, 0),
                success(bytes)
                {
                    TEST_ASSERT_EQUAL(4, bytes);
                    TEST_ASSERT_EQUAL_MEMORY("ping", read_buffer, 4);
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_service_handler_socket_accept_waits_for_client_connect(void)
{
    match_result(IOSubsystem__register(kernel),
        success_do_nothing(),
        error_test_fail()
    );
    SocketRegistry__reset(test_socket_registry());

    ProcessPtr router = dummy_process(1, cpu, PROC_STATE_READY);
    router->process_class = PROCESS_CLASS_SERVICE;
    match_result(Scheduler__schedule(kernel->scheduler, router),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_REGISTER_SOCKET_ROUTER, 0, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr listener_process = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, listener_process),
        success_do_nothing(),
        error_test_fail()
    );

    IOPathRequest listen_path = IOPathRequest__new(&bstr("socket://proc/waiting"), IO_OPEN_BIND | IO_OPEN_LISTEN);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, listener_process), IO_OPEN, (Anything)&listen_path, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    IOSocketRouterRequest listen_request = test_socket_router_take_request(router);
    IOSocketRouterCompletion listen_completion =
        IOSocketRouterCompletion__new(listen_request.request_id, IO_SOCKET_OK, listen_request.handle);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_COMPLETE, (Anything)&listen_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    test_resume_process(listener_process);
    FileDescriptor listener_fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);
    clear_deferred_syscall_result();

    match_result(
        IO__handler(kernel, Some(ProcessPtr, listener_process), IO_ACCEPT, listener_fd, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, listener_process->state);

    ProcessPtr client_process = dummy_process(3, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, client_process),
        success_do_nothing(),
        error_test_fail()
    );

    IOPathRequest connect_path = IOPathRequest__new(&bstr("socket://proc/waiting"), IO_OPEN_READ | IO_OPEN_WRITE);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, client_process), IO_OPEN, (Anything)&connect_path, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    IOSocketRouterRequest connect_request = test_socket_router_take_request(router);
    TEST_ASSERT_EQUAL(IO_SOCKET_ROUTER_CONNECT, connect_request.operation);
    IOSocketRouterCompletion connect_completion =
        IOSocketRouterCompletion__new(connect_request.request_id, IO_SOCKET_OK, listen_request.handle);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, router), SERVICE_SOCKET_COMPLETE, (Anything)&connect_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    test_resume_process(client_process);
    FileDescriptor client_fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);
    clear_deferred_syscall_result();
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, listener_process->state);

    test_resume_process(listener_process);
    FileDescriptor accepted_fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);

    match_result(
        IO__handler(kernel, Some(ProcessPtr, client_process), IO_WRITE, client_fd,
                    (Anything)BString__cstr(&bstr("ping")), BString__length(&bstr("ping")), 0),
        success(bytes) { TEST_ASSERT_EQUAL(4, bytes); },
        error_test_fail()
    );

    char read_buffer[8] = {0};
    match_result(
        IO__handler(kernel, Some(ProcessPtr, listener_process), IO_READ, accepted_fd, (Anything)read_buffer, 4, 0),
        success(bytes)
        {
            TEST_ASSERT_EQUAL(4, bytes);
            TEST_ASSERT_EQUAL_MEMORY("ping", read_buffer, 4);
        },
        error_test_fail()
    );
}

void test_service_handler_userspace_filesystem_list_dir_uses_shared_entries_buffer(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);
    match_result(
        IOSubsystem__register_device(kernel->io_subsystem, &bstr("/"), &root->file_io.base_io, &root->state->state),
        success_do_nothing(),
        error_test_fail()
    );

    ProcessPtr filesystem_service = dummy_process(1, cpu, PROC_STATE_READY);
    filesystem_service->process_class = PROCESS_CLASS_SERVICE;
    ServiceFilesystemRegistration registration = test_filesystem_registration("/dev/virtio-fs", "ext2", "/");

    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_REGISTER_FILESYSTEM,
                         (Anything)&registration, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr requester = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, requester),
        success_do_nothing(),
        error_test_fail()
    );

    UserAddress entries_buffer =
        unwrap_result(Process__alloc_user_pages(requester, sizeof(DirectoryEntry) * 4), NULL_USER_ADDRESS);
    IOListDirRequest list_request = test_list_dir_request("/", (DirectoryEntryPtr)entries_buffer.virtual_addr, 4);

    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_LIST_DIR, (Anything)&list_request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, requester->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_EVENT_FILESYSTEM_IO, event.type);
        },
        error_test_fail()
    );

    ServiceFilesystemRequest filesystem_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST, event.source,
                         (Anything)&filesystem_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_LIST_DIR, filesystem_request.operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio-fs"), filesystem_request.device_path);
            TEST_ASSERT_EQUAL_BSTRING(bstr(""), filesystem_request.path);
            TEST_ASSERT_NOT_EQUAL(0, filesystem_request.entries);
        },
        error_test_fail()
    );

    match_result(
        Process__device_address(requester, entries_buffer.virtual_addr),
        success(requester_phys)
        {
            match_result(
                Process__device_address(filesystem_service, filesystem_request.entries),
                success(service_phys) { TEST_ASSERT_EQUAL(requester_phys, service_phys); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DirectoryEntryPtr entries = ptr(entries_buffer.kernel_addr.virtual_addr, DirectoryEntry);
    entries[0] = DirectoryEntry__new(IO_DIRENT_ATTR_NONE, PROCESS_USER_ROOT, PROCESS_GROUP_ROOT,
                                     IO_MODE_TYPE_FILE | 0644, &bstr("HELLO.TXT"));

    ServiceFilesystemCompletion completion =
        ServiceFilesystemCompletion__new(filesystem_request.request_id, SERVICE_OK, 1);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                         (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, requester->state);

    test_resume_process(requester);
    TEST_ASSERT_EQUAL(1, unwrap_result(deferred_syscall_result(), (Anything)-999));
    TEST_ASSERT_EQUAL_STRING("HELLO.TXT", entries[0].name);

    match_result(
        Process__device_address(filesystem_service, filesystem_request.entries),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_NOT_FOUND, code); }
    );
}

void test_service_handler_userspace_filesystem_open_read_close_file(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);
    match_result(
        IOSubsystem__register_device(kernel->io_subsystem, &bstr("/"), &root->file_io.base_io, &root->state->state),
        success_do_nothing(),
        error_test_fail()
    );

    ProcessPtr filesystem_service = dummy_process(1, cpu, PROC_STATE_READY);
    filesystem_service->process_class = PROCESS_CLASS_SERVICE;
    ServiceFilesystemRegistration registration = test_filesystem_registration("/dev/virtio-fs", "ext2", "/");

    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_REGISTER_FILESYSTEM,
                         (Anything)&registration, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr requester = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, requester),
        success_do_nothing(),
        error_test_fail()
    );

    BString open_path = bstring("/hello.txt");
    IOPathRequest open_request = IOPathRequest__new(&open_path, IO_OPEN_READ);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_OPEN, (Anything)&open_request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    ServiceEvent open_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&open_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceFilesystemRequest filesystem_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST,
                         open_event.source, (Anything)&filesystem_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_OPEN, filesystem_request.operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("hello.txt"), filesystem_request.path);
            TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, filesystem_request.flags);
        },
        error_test_fail()
    );

    ServiceFilesystemCompletion open_completion =
        ServiceFilesystemCompletion__new(filesystem_request.request_id, SERVICE_OK, 0);
    open_completion.mode = IO_MODE_TYPE_FILE | 0644;
    open_completion.size = 12;
    open_completion.block[0] = 12;
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                         (Anything)&open_completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, requester->state);
    test_resume_process(requester);
    FileDescriptor fd = (FileDescriptor)unwrap_result(deferred_syscall_result(), (Anything)-999);
    TEST_ASSERT_GREATER_OR_EQUAL(0, fd);

    UserAddress read_buffer = unwrap_result(Process__alloc_user_pages(requester, 16), NULL_USER_ADDRESS);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_READ, (Anything)fd, read_buffer.virtual_addr, 5, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    ServiceEvent read_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&read_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceFilesystemRequest read_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST,
                         read_event.source, (Anything)&read_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_READ, read_request.operation);
            TEST_ASSERT_EQUAL_UINT64(12, read_request.block);
            TEST_ASSERT_EQUAL_UINT64(0, read_request.block_offset);
            TEST_ASSERT_EQUAL_UINT64(5, read_request.size);
            TEST_ASSERT_NOT_EQUAL(0, read_request.buffer);
        },
        error_test_fail()
    );

    match_result(
        Process__device_address(requester, read_buffer.virtual_addr),
        success(requester_phys)
        {
            match_result(
                Process__device_address(filesystem_service, read_request.buffer),
                success(service_phys) { TEST_ASSERT_EQUAL(requester_phys, service_phys); },
                error_test_fail()
            );
        },
        error_test_fail()
    );

    DataPtr data = ptr(read_buffer.kernel_addr.virtual_addr, Data);
    memcpy(data, "hello", 5);
    ServiceFilesystemCompletion read_completion =
        ServiceFilesystemCompletion__new(read_request.request_id, SERVICE_OK, 5);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                         (Anything)&read_completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, requester->state);
    test_resume_process(requester);
    TEST_ASSERT_EQUAL(5, unwrap_result(deferred_syscall_result(), (Anything)-999));
    TEST_ASSERT_EQUAL_MEMORY("hello", data, 5);

    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_CLOSE, (Anything)fd, 0, 0, 0),
        success(result) { TEST_ASSERT_EQUAL(fd, result); },
        error_test_fail()
    );
}

void test_service_handler_userspace_filesystem_write_mkdir_and_remove(void)
{
    FilesystemPtr root = RootFilesystem__heap_new();
    FilesystemIOState__heap_new(root);
    kernel->io_subsystem->root_filesystem = Some(FilesystemPtr, root);
    match_result(
        IOSubsystem__register_device(kernel->io_subsystem, &bstr("/"), &root->file_io.base_io, &root->state->state),
        success_do_nothing(),
        error_test_fail()
    );

    ProcessPtr filesystem_service = dummy_process(1, cpu, PROC_STATE_READY);
    filesystem_service->process_class = PROCESS_CLASS_SERVICE;
    ServiceFilesystemRegistration registration = test_filesystem_registration("/dev/virtio-fs", "ext2", "/");

    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_REGISTER_FILESYSTEM,
                         (Anything)&registration, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr requester = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, requester),
        success_do_nothing(),
        error_test_fail()
    );

    IOPathRequest open_request = IOPathRequest__new(&bstr("/new.txt"), IO_OPEN_WRITE);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_OPEN, (Anything)&open_request, 0, 0, 0),
        success(fd_any)
        {
            FileDescriptor fd = (FileDescriptor)fd_any;
            UserAddress write_buffer = unwrap_result(Process__alloc_user_pages(requester, 16), NULL_USER_ADDRESS);
            DataPtr data = ptr(write_buffer.kernel_addr.virtual_addr, Data);
            memcpy(data, "new data", 8);

            match_result(
                IO__handler(kernel, Some(ProcessPtr, requester), IO_WRITE, (Anything)fd, write_buffer.virtual_addr, 8,
                            0),
                success_test_fail(),
                error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
            );

            ServiceEvent write_event = {0};
            match_result(
                Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&write_event, 0,
                                 0, 0),
                success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
                error_test_fail()
            );

            ServiceFilesystemRequest write_request = {0};
            match_result(
                Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST,
                                 write_event.source, (Anything)&write_request, 0, 0),
                success(code)
                {
                    TEST_ASSERT_EQUAL(SERVICE_OK, code);
                    TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_WRITE, write_request.operation);
                    TEST_ASSERT_EQUAL_BSTRING(bstr("new.txt"), write_request.path);
                    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE, write_request.flags);
                    TEST_ASSERT_EQUAL_UINT64(8, write_request.size);
                    TEST_ASSERT_NOT_EQUAL(0, write_request.buffer);
                },
                error_test_fail()
            );

            match_result(
                Process__device_address(requester, write_buffer.virtual_addr),
                success(requester_phys)
                {
                    match_result(
                        Process__device_address(filesystem_service, write_request.buffer),
                        success(service_phys) { TEST_ASSERT_EQUAL(requester_phys, service_phys); },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );

            ServiceFilesystemCompletion write_completion =
                ServiceFilesystemCompletion__new(write_request.request_id, SERVICE_OK, 8);
            write_completion.mode = IO_MODE_TYPE_FILE | 0644;
            write_completion.size = 8;
            write_completion.block[0] = 13;
            match_result(
                Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                                 (Anything)&write_completion, 0, 0, 0),
                success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, requester->state);
            test_resume_process(requester);
            TEST_ASSERT_EQUAL(8, unwrap_result(deferred_syscall_result(), (Anything)-999));
        },
        error_test_fail()
    );

    IOPathRequest mkdir_request = IOPathRequest__new(&bstr("/docs"), 0);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_MKDIR, (Anything)&mkdir_request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    ServiceEvent mkdir_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&mkdir_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceFilesystemRequest mkdir_service_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST,
                         mkdir_event.source, (Anything)&mkdir_service_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_MKDIR, mkdir_service_request.operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("docs"), mkdir_service_request.path);
        },
        error_test_fail()
    );

    ServiceFilesystemCompletion mkdir_completion =
        ServiceFilesystemCompletion__new(mkdir_service_request.request_id, SERVICE_OK, IO_OK);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                         (Anything)&mkdir_completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    test_resume_process(requester);
    TEST_ASSERT_EQUAL(IO_OK, unwrap_result(deferred_syscall_result(), (Anything)-999));

    IOPathRequest remove_request = IOPathRequest__new(&bstr("/docs"), IO_REMOVE_RECURSIVE);
    match_result(
        IO__handler(kernel, Some(ProcessPtr, requester), IO_REMOVE, (Anything)&remove_request, 0, 0, 0),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, code); }
    );

    ServiceEvent remove_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_WAIT, (Anything)&remove_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceFilesystemRequest remove_service_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_GET_REQUEST,
                         remove_event.source, (Anything)&remove_service_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_FILESYSTEM_REMOVE, remove_service_request.operation);
            TEST_ASSERT_EQUAL_BSTRING(bstr("docs"), remove_service_request.path);
            TEST_ASSERT_EQUAL_UINT64(IO_REMOVE_RECURSIVE, remove_service_request.flags);
        },
        error_test_fail()
    );

    ServiceFilesystemCompletion remove_completion =
        ServiceFilesystemCompletion__new(remove_service_request.request_id, SERVICE_OK, IO_OK);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, filesystem_service), SERVICE_FILESYSTEM_COMPLETE,
                         (Anything)&remove_completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );
    test_resume_process(requester);
    TEST_ASSERT_EQUAL(IO_OK, unwrap_result(deferred_syscall_result(), (Anything)-999));
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
        BlockDevicePtr device = (BlockDevicePtr)context->owner_context;
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
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-async", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-async")),
        some(found) { device = found; },
        none_test_fail()
    );

    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = test_kernel_context_for_process(reader);
    block_context->owner_context = 0;

    BlockDevice__set_current_context(&block_context->kernel_context);
    match_result(
        BlockDevice__read_async(device, 9, 1, block_context, async_read_continuation),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything)&request,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);

    test_resume_process(reader);

    TEST_ASSERT_TRUE(async_continuation_called);
    TEST_ASSERT_TRUE(async_continuation_has_buffer);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(77, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_chained_async_read_parks_from_block_context(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-chain", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-chain")),
        some(found) { device = found; },
        none_test_fail()
    );

    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = test_kernel_context_for_process(reader);
    block_context->owner_context = (Anything)device;

    BlockDevice__set_current_context(&block_context->kernel_context);
    match_result(
        BlockDevice__read_async(device, 9, 1, block_context, chained_async_read_continuation),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent first_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&first_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest first_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, first_event.source,
                         (Anything)&first_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(9, first_request.block);
        },
        error_test_fail()
    );

    ServiceBlockCompletion first_completion = {
        .request_id = first_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&first_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent second_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&second_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest second_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, second_event.source,
                         (Anything)&second_request, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(10, second_request.block);
        },
        error_test_fail()
    );

    ServiceBlockCompletion second_completion = {
        .request_id = second_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&second_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_TRUE(chained_async_continuation_called);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(99, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_parallel_async_reads_complete_out_of_order_without_resuming_unready_request(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-parallel", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-parallel")),
        some(found) { device = found; },
        none_test_fail()
    );

    BlockIOContextPtr first_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    first_context->kernel_context = test_kernel_context_for_process(reader);
    first_context->owner_context = 0;

    BlockIOContextPtr second_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    second_context->kernel_context = test_kernel_context_for_process(reader);
    second_context->owner_context = 0;

    BlockDevice__set_current_context(&first_context->kernel_context);
    match_result(
        BlockDevice__read_async(device, 9, 1, first_context, first_parallel_async_continuation),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    match_result(
        BlockDevice__read_async(device, 10, 1, second_context, second_parallel_async_continuation),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent first_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&first_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceEvent second_event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&second_event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest first_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, first_event.source,
                         (Anything)&first_request, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest second_request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, second_event.source,
                         (Anything)&second_request, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(9, first_request.block);
    TEST_ASSERT_EQUAL(10, second_request.block);

    ServiceBlockCompletion second_completion = {
        .request_id = second_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&second_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_FALSE(first_parallel_async_called);
    TEST_ASSERT_TRUE(second_parallel_async_called);
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    TEST_ASSERT_TRUE(Process__is_parked(reader));
    TEST_ASSERT_EQUAL_UINT64(1, ParkedOperations__len(&reader->parked_operations));
    TEST_ASSERT_EQUAL(202, unwrap_result(deferred_syscall_result(), (Anything)-999));

    ServiceBlockCompletion first_completion = {
        .request_id = first_request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&first_completion, 0, 0,
                         0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);
    test_resume_process(reader);

    TEST_ASSERT_TRUE(first_parallel_async_called);
    TEST_ASSERT_FALSE(Process__is_parked(reader));
    TEST_ASSERT_EQUAL_UINT64(0, ParkedOperations__len(&reader->parked_operations));
    TEST_ASSERT_EQUAL(101, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_failed_block_completion_unparks_waiting_reader_with_error(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-park-fail", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr reader = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, reader),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-park-fail")),
        some(found) { device = found; },
        none_test_fail()
    );

    KernelContext context = test_kernel_context_for_process(reader);
    Data target[BLOCK_DEVICE_BLOCK_SIZE] = {0};
    BlockDevice__set_current_context(&context);
    match_result(
        BlockDevice__read(device, 9, 1, target),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, reader->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockCompletion completion = {
        .request_id = event.source,
        .status = SERVICE_BLOCK_REQUEST_FAILED,
        .block_count = 0,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, reader->state);

    test_resume_process(reader);
    TEST_ASSERT_EQUAL(BLOCK_DEVICE_IO_ERROR, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

void test_service_handler_block_completion_runs_async_write_continuation(void)
{
    ProcessPtr service = dummy_process(1, cpu, PROC_STATE_READY);
    service->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration registration = test_block_registration("/dev/virtio-async-write", 128);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&registration, 0,
                         0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ProcessPtr writer = dummy_process(2, cpu, PROC_STATE_READY);
    match_result(Scheduler__schedule(kernel->scheduler, writer),
        success_do_nothing(),
        error_test_fail()
    );

    BlockDevicePtr device = NULL;
    match_optional(
        TestBlockDeviceRegistry__find(bstring("/dev/virtio-async-write")),
        some(found) { device = found; },
        none_test_fail()
    );

    Data source[BLOCK_DEVICE_BLOCK_SIZE] = {0};
    source[0] = 0x5a;
    BlockIOContextPtr block_context = ptr(Kernel__alloc_kernel(sizeof(BlockIOContext)), BlockIOContext);
    block_context->kernel_context = test_kernel_context_for_process(writer);
    block_context->owner_context = 0;

    BlockDevice__set_current_context(&block_context->kernel_context);
    match_result(
        BlockDevice__write_async(device, 12, 1, source, block_context, async_write_continuation),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(BLOCK_DEVICE_REQUEST_PENDING, code); }
    );
    BlockDevice__clear_current_context();

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, writer->state);

    ServiceEvent event = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    ServiceBlockRequest request = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_GET_REQUEST, event.source, (Anything)&request,
                         0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
            TEST_ASSERT_EQUAL(SERVICE_BLOCK_WRITE, request.operation);
            TEST_ASSERT_EQUAL(12, request.block);
            TEST_ASSERT_EQUAL(1, request.block_count);
        },
        error_test_fail()
    );

    ServiceBlockCompletion completion = {
        .request_id = request.request_id,
        .status = SERVICE_OK,
        .block_count = 1,
    };
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_BLOCK_COMPLETE, (Anything)&completion, 0, 0, 0),
        success(code) { TEST_ASSERT_EQUAL(SERVICE_OK, code); },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, writer->state);

    test_resume_process(writer);

    TEST_ASSERT_TRUE(async_write_continuation_called);
    TEST_ASSERT_EQUAL(1, async_continuation_blocks);
    TEST_ASSERT_EQUAL(88, unwrap_result(deferred_syscall_result(), (Anything)-999));
}

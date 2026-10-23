#include "unity.h"
#include <string.h>

#include "../../../kernel/syscall/service_handler.h"
#include "../../../kernel/service/service.h"
#include "../../../kernel/service/service_directory.h"
#include "../../../kernel/scheduler/context_switch.h"
#include "../../../kernel/arch/process.h"
#include "../../../kernel/kalloc.h"
#include "../../../kernel/service/device_registry.h"
#include "../../../common_includes/syscalls/service.h"
#include "../../../common_includes/syscalls/socket.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/process.h"

#define Service__handler(kernel, process, ...) Service__handler((kernel), &(process), __VA_ARGS__)

static KernelPtr kernel = NULL;
static const CPUID cpu = 0;

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

void Arch__reset_irq_tracking();
uint64_t Arch__mask_count();
uint64_t Arch__unmask_count();
uint64_t Arch__enabled_irq();
uint64_t Arch__enable_count();

void setUp(void)
{
    clear_panic_state();
    clear_deferred_syscall_result();
    current_process_reset();
    Arch__reset_irq_tracking();
    kernel = init_test_kernel();
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROCESS_CLASS_SERVICE, process->process_class);
}

void test_service_handler_rejects_retired_filesystem_and_socket_brokers(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    for (Anything operation = 12; operation <= 19; operation++)
    {
        if (operation == 15 || operation == 16)
        {
            continue;
        }
        match_result(
            Service__handler(kernel, Some(ProcessPtr, process), operation, 0, 0, 0, 0),
            success_test_fail(),
            error(code, message)
            {
                TEST_ASSERT_EQUAL(SERVICE_OPERATION_NOT_SUPPORTED, code);
            }
        );
    }
}

void test_service_handler_become_service_denies_unprivileged_process(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_NORMAL;
    Process__set_identity(process, 1000, 1000, PROCESS_CAP_NONE);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_BECOME_SERVICE, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );
    Process__set_identity(client, 1000, 1000, PROCESS_CAP_NONE);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, 99999, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );
    Process__set_identity(service, 1000, 1000, PROCESS_CAP_NONE);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_BLOCK_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );

    Process__set_hund_identity(service);
    service->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(client);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_MOUNT_CLIENT, client->pid, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );
    Process__set_identity(client, 1000, 1000, PROCESS_CAP_NONE);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_MOUNT_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_MOUNT_CLIENT, 99999, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
    );
    service->process_class = PROCESS_CLASS_NORMAL;
    match_result(
        Service__handler(kernel, Some(ProcessPtr, service), SERVICE_AUTHORIZE_MOUNT_CLIENT, client->pid, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_NOT_SERVICE_PROCESS, code);
        }
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_PERMISSION_DENIED, code);
        }
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

    Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);

    ServiceEvent queued = (ServiceEvent){
        .type = SERVICE_EVENT_MESSAGE,
        .source = 7,
        .data = 11,
    };

    match_result(
        Scheduler__service_enqueue(kernel->scheduler, process, &queued),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT_READY, (Anything)&request, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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

    Scheduler__schedule_on_cpu(kernel->scheduler, waiter, cpu);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, waiter), SERVICE_WAIT_READY, (Anything)&request, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, waiter->state);

    match_result(
        Service__handler(kernel, Some(ProcessPtr, publisher), SERVICE_PUBLISH_READY, (Anything)&request, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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

    Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu);

    match_result(
        Scheduler__subscribe_irq(kernel->scheduler, process, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Scheduler__send_irq_event(kernel->scheduler, irq),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(1, Arch__mask_count());

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(SERVICE_EVENT_IRQ, target.type);
    TEST_ASSERT_EQUAL(irq, target.source);
    TEST_ASSERT_TRUE(Process__is_handling_service_irq(process));
    TEST_ASSERT_EQUAL(0, Arch__unmask_count());

    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&target, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_EMPTY, code);
        }
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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
        success(status)
        {
            TEST_ASSERT_EQUAL(IRQ_DISPATCH_RESCHEDULE, status);
        },
        error_test_fail()
    );

    ServiceEvent event = (ServiceEvent){0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_WAIT, (Anything)&event, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
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
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    match_result(
        Service__handler(kernel, Some(ProcessPtr, second_process), SERVICE_SUBSCRIBE_IRQ, irq, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_IRQ_ALREADY_REGISTERED, code);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(1, Arch__enable_count());
}

void test_service_handler_registers_interrupt_manager_with_plic_mapping(void)
{
    const Address plic_physical_base = 0x0c000000;
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(process);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu),
        success_do_nothing(),
        error_test_fail()
    );
    SystemInfo system_info = SystemInfo__new();
    const uint64_t target_hart_id = 3;
    PlicInterruptControllerInfo plic = PlicInterruptControllerInfo__new(plic_physical_base, 0, 95, target_hart_id);
    system_info.interrupt_controller = InterruptControllerInfo__new_plic(&plic);
    kernel->system_info = Some(SystemInfo, system_info);
    kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, system_info.interrupt_controller);

    ServiceInterruptManagerRegistration registration = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_INTERRUPT_MANAGER, (Anything)&registration,
                         0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_NOT_EQUAL(0, registration.event_queue);
    TEST_ASSERT_NOT_EQUAL(0, registration.distributor);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(USER_ALLOC_BASE, registration.distributor);
    TEST_ASSERT_LESS_THAN_UINT64(HUND_SERVICE_DIRECTORY_VADDR, registration.distributor);
    TEST_ASSERT_EQUAL(PROCESS_CLASS_REALTIME_SERVICE, process->process_class);
    match_result(
        Process__device_address(process, registration.distributor),
        success(device_address)
        {
            TEST_ASSERT_EQUAL_UINT64(plic_physical_base, device_address);
        },
        error_test_fail()
    );
}

void test_service_handler_maps_only_plic_priorities_independently_of_target_hart(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(process);
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, process, cpu),
        success_do_nothing(),
        error_test_fail()
    );
    SystemInfo system_info = SystemInfo__new();
    PlicInterruptControllerInfo plic = PlicInterruptControllerInfo__new(0x0c000000, 0, 101, 31);
    system_info.interrupt_controller = InterruptControllerInfo__new_plic(&plic);
    kernel->system_info = Some(SystemInfo, system_info);
    kernel->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, system_info.interrupt_controller);

    ServiceInterruptManagerRegistration registration = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_INTERRUPT_MANAGER, (Anything)&registration,
                         0, 0, 0),
        success(code)
        {
            TEST_ASSERT_EQUAL(SERVICE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_NOT_EQUAL(0, registration.event_queue);
    TEST_ASSERT_NOT_EQUAL(0, registration.distributor);
    TEST_ASSERT_EQUAL(PROCESS_CLASS_REALTIME_SERVICE, process->process_class);
    match_result(
        Process__device_address(process, registration.distributor + 0xffc),
        success(device_address)
        {
            TEST_ASSERT_EQUAL_UINT64(0x0c000ffc, device_address);
        },
        error_test_fail()
    );
    TEST_ASSERT_TRUE(
        is_nothing(Process__readable_mmio_mapping(process, registration.distributor + 0x1000, sizeof(uint32_t))));
    TEST_ASSERT_TRUE(
        is_nothing(Process__writable_mmio_mapping(process, registration.distributor + 0x2080, sizeof(uint32_t))));
}

void test_service_handler_does_not_promote_failed_interrupt_manager_registration(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(process);
    kernel->system_info = None(SystemInfo);

    ServiceInterruptManagerRegistration registration = {0};
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_INTERRUPT_MANAGER, (Anything)&registration,
                         0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_MAP_FAILED, code);
        }
    );

    TEST_ASSERT_EQUAL(PROCESS_CLASS_SERVICE, process->process_class);
}

void test_service_handler_publishes_block_metadata_without_io_transport(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    ServiceBlockDeviceRegistration request = ServiceBlockDeviceRegistration__new(&bstr("/dev/test"), 512, 128);
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success_do_nothing(),
        error_test_fail()
    );
    DeviceRegistryPtr directory = k_v_ptr(kernel->device_registry.mapping, DeviceRegistry);
    match_result(
        DeviceRegistry__at(directory, 0),
        success(record)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/test"), record.path);
            TEST_ASSERT_EQUAL_BSTRING(bstr("hd0"), record.alias);
            TEST_ASSERT_EQUAL_UINT64(process->pid, record.owner_pid);
            TEST_ASSERT_EQUAL_UINT64(128, record.block_count);
        },
        error_test_fail()
    );
    match_result(
        Service__handler(kernel, Some(ProcessPtr, process), SERVICE_REGISTER_BLOCK_DEVICE, (Anything)&request, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code);
        }
    );
    KernelDeviceRegistry__remove_process(&kernel->device_registry, process->pid);
    match_result(
        DeviceRegistry__at(directory, 0),
        success(record)
        {
            TEST_ASSERT_EQUAL(DEVICE_ABSENT, record.kind);
        },
        error_test_fail()
    );
}

void test_service_handler_rejects_retired_block_transport_operations(void)
{
    ProcessPtr process = dummy_process(1, cpu, PROC_STATE_READY);
    process->process_class = PROCESS_CLASS_SERVICE;
    for (uint64_t operation = 0; operation < 8; operation++)
    {
        const ServiceOperation retired[] = {SERVICE_BLOCK_GET_REQUEST,       SERVICE_BLOCK_COMPLETE,
                                            SERVICE_BLOCK_READ_DEVICE,       SERVICE_BLOCK_WRITE_DEVICE,
                                            SERVICE_BLOCK_READ_DEVICE_ASYNC, SERVICE_BLOCK_WRITE_DEVICE_ASYNC,
                                            SERVICE_BLOCK_CONNECT,           SERVICE_BLOCK_DISCONNECT};
        match_result(
            Service__handler(kernel, Some(ProcessPtr, process), retired[operation], 0, 0, 0, 0),
            success_test_fail(),
            error(code, message)
            {
                TEST_ASSERT_EQUAL(SERVICE_OPERATION_NOT_SUPPORTED, code);
            }
        );
    }
}

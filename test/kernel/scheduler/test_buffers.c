#include "unity.h"
#include "../../../kernel/kernel_core.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"

static KernelPtr kernel;
static ProcessPtr manager;
static ProcessPtr server;
static ProcessPtr client;
static BuffersPtr buffers;

static Address operation(ProcessPtr caller, uint64_t op, uint64_t first, uint64_t second, uint64_t third,
                         uint64_t fourth)
{
    match_result(
        Buffers__operation(buffers, kernel->scheduler, caller, op, first, second, third, fourth),
        success(value)
        {
            return value;
        },
        error_test_fail()
    );
    return 0;
}

void setUp(void)
{
    KAllocStub__reset_virtual_translation();
    KAllocStub__set_free_failure(false);
    KAllocStub__set_user_alloc_failure(false);
    PageTableStub__fail_mapping_after(-1);
    kernel = init_test_kernel();
    manager = dummy_process(10, 0, PROC_STATE_READY);
    server = dummy_process(20, 0, PROC_STATE_READY);
    client = dummy_process(30, 0, PROC_STATE_READY);
    manager->name = bstr("/sbin/services/hunde-puffer");
    manager->process_class = server->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(manager);
    Process__set_hund_identity(server);
    Scheduler__schedule_on_cpu(kernel->scheduler, manager, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, server, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, client, 0);
    buffers = &kernel->scheduler->buffers;
    TEST_ASSERT_EQUAL_HEX64(BUFFER_EVENTS_ADDRESS, operation(manager, MEM_BUFFER_REGISTER, 0, 0, 0, 0));
}

void tearDown(void)
{
    PageTableStub__fail_mapping_after(-1);
}

static bool mapped(ProcessPtr process, Address address, bool write)
{
    return check_address_mapped(ProcessPageTable__root_table(&process->page_table), address, true, write, false,
                                BUFFER_SIZE);
}

static BufferID create(BufferDirection direction)
{
    return operation(manager, MEM_BUFFER_CREATE, server->pid, client->pid, direction, 0);
}

void test_buffers_map_same_address_with_directional_permissions_only_to_participants(void)
{
    BufferID read = create(BUFFER_READ);
    BufferDescriptor descriptor = BufferDescriptor__new(read, BUFFER_READ);
    TEST_ASSERT_TRUE(mapped(server, descriptor.address, true));
    TEST_ASSERT_TRUE(mapped(client, descriptor.address, false));
    TEST_ASSERT_FALSE(mapped(manager, descriptor.address, true));
    BufferID write = create(BUFFER_WRITE);
    descriptor = BufferDescriptor__new(write, BUFFER_WRITE);
    TEST_ASSERT_TRUE(mapped(server, descriptor.address, false));
    TEST_ASSERT_TRUE(mapped(client, descriptor.address, true));
    TEST_ASSERT_EQUAL_HEX64(descriptor.address,
                            operation(client, MEM_BUFFER_VALIDATE, write, BUFFER_WRITE, server->pid, 0));
    match_result(
        Buffers__operation(buffers, kernel->scheduler, client, MEM_BUFFER_CREATE, server->pid, client->pid, BUFFER_READ,
                           0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
}

void test_exit_keeps_survivor_backing_until_release_then_reuse_invalidates_old_generation(void)
{
    BufferID id = create(BUFFER_READ);
    TEST_ASSERT_FALSE(Buffers__has_pending_notifications(buffers, manager));
    BufferDescriptor descriptor = BufferDescriptor__new(id, BUFFER_READ);
    match_optional(
        ManagedBuffers__at(&buffers->allocations, id % BUFFER_LIMIT),
        some(buffer)
        {
            Kernel__memset(buffer.memory.kernel_addr.virtual_addr, 0xa5, BUFFER_SIZE);
        },
        none_test_fail()
    );
    Buffers__exit(buffers, kernel->scheduler, client);
    TEST_ASSERT_TRUE(Buffers__has_pending_notifications(buffers, manager));
    TEST_ASSERT_FALSE(Buffers__has_pending_notifications(buffers, server));
    TEST_ASSERT_FALSE(mapped(client, descriptor.address, false));
    TEST_ASSERT_TRUE(mapped(server, descriptor.address, true));
    TEST_ASSERT_EQUAL(BUFFER_SERVER_ATTACHED | BUFFER_RETIRING, operation(manager, MEM_BUFFER_STATE, id, 0, 0, 0));
    match_result(
        Buffers__operation(buffers, kernel->scheduler, manager, MEM_BUFFER_FREE, id, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_BUSY, code);
        }
    );
    KernelNotificationQueuePtr events = k_v_ptr(buffers->notifications, KernelNotificationQueue);
    match_optional(
        KernelNotificationQueue__pop(events),
        some(event)
        {
            TEST_ASSERT_EQUAL_UINT64(id, event.sequence);
        },
        none_test_fail()
    );
    operation(manager, MEM_BUFFER_DROP, id, server->pid, 0, 0);
    BufferID replacement = operation(manager, MEM_BUFFER_CREATE, server->pid, client->pid, BUFFER_WRITE, id);
    TEST_ASSERT_NOT_EQUAL(id, replacement);
    TEST_ASSERT_EQUAL_UINT64(id % BUFFER_LIMIT, replacement % BUFFER_LIMIT);
    match_optional(
        ManagedBuffers__at(&buffers->allocations, replacement % BUFFER_LIMIT),
        some(buffer)
        {
            TEST_ASSERT_EACH_EQUAL_UINT8(0, (void *)buffer.memory.kernel_addr.virtual_addr, BUFFER_SIZE);
        },
        none_test_fail()
    );
    match_result(
        Buffers__operation(buffers, kernel->scheduler, client, MEM_BUFFER_VALIDATE, id, BUFFER_READ, server->pid, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
}

void test_mapping_failure_rolls_back_first_participant_and_leaves_reclaimable_allocation(void)
{
    PageTableStub__fail_mapping_after(1);
    match_result(
        Buffers__operation(buffers, kernel->scheduler, manager, MEM_BUFFER_CREATE, server->pid, client->pid,
                           BUFFER_READ, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_NO_MEMORY, code);
        }
    );
    TEST_ASSERT_FALSE(mapped(server, BUFFER_BASE, true));
    TEST_ASSERT_FALSE(mapped(client, BUFFER_BASE, false));
    BufferID id = operation(manager, MEM_BUFFER_AT, 0, 0, 0, 0);
    TEST_ASSERT_EQUAL(0, operation(manager, MEM_BUFFER_STATE, id, 0, 0, 0));
    TEST_ASSERT_EQUAL(BUFFER_OK, operation(manager, MEM_BUFFER_FREE, id, 0, 0, 0));
}

void test_kernel_reads_require_expected_server_and_reject_retired_buffers(void)
{
    BufferID id = operation(manager, MEM_BUFFER_CREATE, server->pid, QUEUE_KERNEL_PID, BUFFER_READ, 0);
    match_result(Buffers__kernel_read(buffers, id, server->pid, BUFFER_SIZE),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Buffers__kernel_read(buffers, id, client->pid, 1),
        success_test_fail(),
        error_test_ok()
    );
    operation(manager, MEM_BUFFER_DROP, id, server->pid, 0, 0);
    match_result(Buffers__kernel_read(buffers, id, server->pid, 1),
        success_test_fail(),
        error_test_ok()
    );
    operation(manager, MEM_BUFFER_DROP, id, QUEUE_KERNEL_PID, 0, 0);
    TEST_ASSERT_EQUAL(BUFFER_OK, operation(manager, MEM_BUFFER_FREE, id, 0, 0, 0));
}

void test_service_buffers_cannot_be_freed_or_reassigned_after_both_services_exit(void)
{
    client->process_class = PROCESS_CLASS_SERVICE;
    BufferID id = create(BUFFER_READ);
    BufferDescriptor descriptor = BufferDescriptor__new(id, BUFFER_READ);
    TEST_ASSERT_EQUAL(BUFFER_RETAINED | BUFFER_CLIENT_ATTACHED | BUFFER_SERVER_ATTACHED,
                      operation(manager, MEM_BUFFER_STATE, id, 0, 0, 0));
    Buffers__exit(buffers, kernel->scheduler, client);
    Buffers__exit(buffers, kernel->scheduler, server);
    TEST_ASSERT_FALSE(mapped(client, descriptor.address, false));
    TEST_ASSERT_FALSE(mapped(server, descriptor.address, true));
    TEST_ASSERT_EQUAL(BUFFER_RETAINED | BUFFER_RETIRING, operation(manager, MEM_BUFFER_STATE, id, 0, 0, 0));
    match_result(
        Buffers__operation(buffers, kernel->scheduler, manager, MEM_BUFFER_FREE, id, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_BUSY, code);
        }
    );
    match_result(
        Buffers__operation(buffers, kernel->scheduler, manager, MEM_BUFFER_CREATE, server->pid, client->pid,
                           BUFFER_READ, id),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_BUSY, code);
        }
    );
}

void test_dma_range_requires_retained_server_lease_direction_and_bounds(void)
{
    BufferID ordinary = create(BUFFER_READ);
    match_result(
        Buffers__operation(buffers, kernel->scheduler, server, MEM_BUFFER_DEVICE_ADDRESS, ordinary, 0, 512,
                           BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
    client->process_class = PROCESS_CLASS_SERVICE;
    BufferID id = create(BUFFER_READ);
    Address backing = 0;
    match_optional(
        ManagedBuffers__at(&buffers->allocations, id % BUFFER_LIMIT),
        some(buffer)
        {
            backing = buffer.memory.kernel_addr.virtual_addr;
        },
        none_test_fail()
    );
    KAllocStub__set_virtual_translation(backing, BUFFER_SIZE, 0x10000000);
    TEST_ASSERT_EQUAL_HEX64(0x10000000, operation(server, MEM_BUFFER_DEVICE_ADDRESS, id, 0, BUFFER_SIZE, BUFFER_READ));
    TEST_ASSERT_EQUAL_HEX64(0x10000200, operation(server, MEM_BUFFER_DEVICE_ADDRESS, id, 512, 512, BUFFER_READ));
    match_result(
        Buffers__operation(buffers, kernel->scheduler, client, MEM_BUFFER_DEVICE_ADDRESS, id, 0, 512, BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
    match_result(
        Buffers__operation(buffers, kernel->scheduler, server, MEM_BUFFER_DEVICE_ADDRESS, id, 0, 512, BUFFER_WRITE),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
    match_result(
        Buffers__operation(buffers, kernel->scheduler, server, MEM_BUFFER_DEVICE_ADDRESS, id, BUFFER_SIZE - 512, 1024,
                           BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_INVALID, code);
        }
    );
    KAllocStub__set_virtual_translation(backing, PAGE_SIZE, 0x10000000);
    match_result(
        Buffers__operation(buffers, kernel->scheduler, server, MEM_BUFFER_DEVICE_ADDRESS, id, 0, BUFFER_SIZE,
                           BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_INVALID, code);
        }
    );
    operation(manager, MEM_BUFFER_DROP, id, client->pid, 0, 0);
    match_result(
        Buffers__operation(buffers, kernel->scheduler, server, MEM_BUFFER_DEVICE_ADDRESS, id, 0, 512, BUFFER_READ),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_DENIED, code);
        }
    );
    operation(manager, MEM_BUFFER_DROP, id, server->pid, 0, 0);
    match_result(
        Buffers__operation(buffers, kernel->scheduler, manager, MEM_BUFFER_FREE, id, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(BUFFER_BUSY, code);
        }
    );
}

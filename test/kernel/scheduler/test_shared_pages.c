#include "unity.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/arch/page_table.h"
#include "../../../kernel/syscall/mem_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../stubs/panic.h"

static KernelPtr kernel;
static ProcessPtr owner;
static ProcessPtr first_peer;
static ProcessPtr second_peer;
static ProcessPtr unrelated;

void setUp(void)
{
    clear_panic_state();
    KAllocStub__reset_allocation_tracking();
    kernel = init_test_kernel_without_filesystem();
    owner = dummy_process(10, 0, PROC_STATE_READY);
    first_peer = dummy_process(20, 0, PROC_STATE_READY);
    second_peer = dummy_process(30, 0, PROC_STATE_READY);
    unrelated = dummy_process(40, 0, PROC_STATE_READY);
    owner->process_class = PROCESS_CLASS_SERVICE;
    Process__set_hund_identity(owner);
    Scheduler__schedule_on_cpu(kernel->scheduler, owner, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, first_peer, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, second_peer, 0);
    Scheduler__schedule_on_cpu(kernel->scheduler, unrelated, 0);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static Address allocate_shared_page(void)
{
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, owner, first_peer->pid, second_peer->pid),
        success(address) { return address; },
        error_test_fail()
    );
    return 0;
}

static bool mapped(ProcessPtr process, Address address)
{
    return check_address_mapped(ProcessPageTable__root_table(&process->page_table), address, true, true, false,
                                MEM_SHARED_PAGE_SIZE);
}

static void release_page(Address address)
{
    match_result(
        Scheduler__release_shared_page(kernel->scheduler, owner, address),
        success(status) { TEST_ASSERT_EQUAL(MEM_OK, status); },
        error_test_fail()
    );
}

void test_shared_page_is_zeroed_and_mapped_only_to_three_participants(void)
{
    Address address = allocate_shared_page();
    TEST_ASSERT_TRUE(mapped(owner, address));
    TEST_ASSERT_TRUE(mapped(first_peer, address));
    TEST_ASSERT_TRUE(mapped(second_peer, address));
    TEST_ASSERT_FALSE(mapped(unrelated, address));
    for_each_fa(ManagedSharedPages, kernel->scheduler->shared_pages, index, page, {
        if (page.address.virtual_addr == address)
        {
            for (size_t offset = 0; offset < MEM_SHARED_PAGE_SIZE; offset++)
            {
                TEST_ASSERT_EQUAL_UINT8(0, *ptr(page.address.kernel_addr.virtual_addr + offset, uint8_t));
            }
        }
    });
}

void test_owner_release_unmaps_every_participant_and_frees_backing_once(void)
{
    Address address = allocate_shared_page();
    size_t before = KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE);
    release_page(address);
    TEST_ASSERT_FALSE(mapped(owner, address));
    TEST_ASSERT_FALSE(mapped(first_peer, address));
    TEST_ASSERT_FALSE(mapped(second_peer, address));
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE));
    match_result(
        Scheduler__release_shared_page(kernel->scheduler, owner, address),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_INVALID_ADDRESS, code); }
    );
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE));
}

void test_peer_exit_preserves_survivor_mapping_until_owner_releases(void)
{
    Address address = allocate_shared_page();
    match_result(Scheduler__exit(kernel->scheduler, first_peer, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(mapped(owner, address));
    TEST_ASSERT_TRUE(mapped(second_peer, address));
    size_t before = KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE);
    release_page(address);
    TEST_ASSERT_FALSE(mapped(second_peer, address));
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE));
}

void test_both_peer_exits_leave_backing_owned_until_release(void)
{
    Address address = allocate_shared_page();
    match_result(Scheduler__exit(kernel->scheduler, first_peer, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Scheduler__exit(kernel->scheduler, second_peer, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(mapped(owner, address));
    size_t before = KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE);
    release_page(address);
    TEST_ASSERT_EQUAL_UINT64(before + 1, KAllocStub__freed_allocation_count_for_size(MEM_SHARED_PAGE_SIZE));
}

void test_owner_exit_revokes_remaining_shared_mappings(void)
{
    Address address = allocate_shared_page();
    match_result(Scheduler__exit(kernel->scheduler, owner, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(mapped(first_peer, address));
    TEST_ASSERT_FALSE(mapped(second_peer, address));
    for_each_fa(ManagedSharedPages, kernel->scheduler->shared_pages, index, page,
                { TEST_ASSERT_FALSE(page.address.valid); });
}

void test_shared_page_creation_requires_privileged_service_and_live_distinct_peers(void)
{
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, unrelated, first_peer->pid, second_peer->pid),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_PERMISSION_DENIED, code); }
    );
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, owner, first_peer->pid, first_peer->pid),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_PEER_NOT_FOUND, code); }
    );
    ProcessID dead_pid = first_peer->pid;
    match_result(Scheduler__exit(kernel->scheduler, first_peer, 0),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, owner, dead_pid, second_peer->pid),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_PEER_NOT_FOUND, code); }
    );
}

void test_unprivileged_service_cannot_allocate_shared_pages(void)
{
    Process__set_user_identity(owner);
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, owner, first_peer->pid, second_peer->pid),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_PERMISSION_DENIED, code); }
    );
}

void test_other_process_cannot_release_shared_page(void)
{
    Address address = allocate_shared_page();
    match_result(
        Scheduler__release_shared_page(kernel->scheduler, first_peer, address),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_PERMISSION_DENIED, code); }
    );
    TEST_ASSERT_TRUE(mapped(first_peer, address));
}

void test_shared_page_allocation_failure_does_not_publish_mappings(void)
{
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        Scheduler__alloc_shared_page(kernel->scheduler, owner, first_peer->pid, second_peer->pid),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(MEM_ALLOCATION_FAILED, code); }
    );
    TEST_ASSERT_FALSE(mapped(first_peer, MEM_SHARED_PAGE_BASE));
    TEST_ASSERT_FALSE(mapped(owner, MEM_SHARED_PAGE_BASE));
}

void test_shared_page_syscalls_allocate_and_release(void)
{
    Optional(ProcessPtr) caller = Some(ProcessPtr, owner);
    match_result(
        Mem__handler(kernel, &caller, MEM_ALLOC_SHARED_PAGE, first_peer->pid, second_peer->pid, 0, 0),
        success(address)
        {
            TEST_ASSERT_TRUE(mapped(first_peer, address));
            match_result(
                Mem__handler(kernel, &caller, MEM_RELEASE_SHARED_PAGE, address, 0, 0, 0),
                success(status) { TEST_ASSERT_EQUAL(MEM_OK, status); },
                error_test_fail()
            );
            TEST_ASSERT_FALSE(mapped(first_peer, address));
        },
        error_test_fail()
    );
}

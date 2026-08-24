#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"

#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"

static KernelPtr kernel;

static Elf load_hello_world_elf(void)
{
    match_optional(Elf, EmbeddedElf__load(bstring("hello_world")), some(elf) { return elf; }, none() {
            TEST_FAIL();
            return (Elf) {};
        });
}

static UserAllocation allocation_at(ProcessPtr process, uint64_t index)
{
    match_optional(UserAllocation, UserAllocations__at(&process->user_allocations, index), some(allocation) { return allocation; }, none() {
            TEST_FAIL();
            return (UserAllocation) {};
        });
}

static UserAddress alloc_user_pages_for_test(ProcessPtr process, size_t size)
{
    match_result(UserAddress, Process__alloc_user_pages(process, size), success(address) { return address; }, error(code, message) {
            TEST_FAIL();
            return (UserAddress) {};
        });
}

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
    KAllocStub__reset_virtual_translation();
    kernel = init_test_kernel_without_filesystem();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_setup_from_elf(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            TEST_ASSERT_EQUAL(Elf__read_header(elf)->entry, process->entry_point.virtual_addr);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                0x400000,
                true,
                false,
                true,
                PAGE_SIZE));
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                BASE_STACK,
                true,
                true,
                false,
                STACK_SIZE));
        }, error_test_fail());
}

void test_setup_tracks_static_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            Optional(UserAllocation) args_allocation = UserAllocations__at(&process->user_allocations, 0);
            Optional(UserAllocation) elf_allocation = UserAllocations__at(&process->user_allocations, 1);
            Optional(UserAllocation) stack_allocation = UserAllocations__at(&process->user_allocations, 2);
            UserAllocation args = allocation_at(process, 0);
            UserAllocation elf = allocation_at(process, 1);
            UserAllocation stack = allocation_at(process, 2);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, args.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, args.size);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_ELF, elf.type);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_STACK, stack.type);
        }, error_test_fail());
}

void test_setup_with_args_sets_up_multiple_argv_entries(void)
{
    Elf elf = load_hello_world_elf();
    ProcessArguments arguments = ProcessArguments__new();
    ProcessArguments__push(&arguments, bstring("one"));
    ProcessArguments__push(&arguments, bstring("two"));
    ProcessArguments__push(&arguments, bstring("three"));

    match_result(ProcessPtr, Process__setup_with_args(kernel, elf, None(ProcessPtr), arguments), success(process) {
            UserAllocation args = allocation_at(process, 0);
            Address * argv = ptr(args.address.kernel_addr.virtual_addr, Address);
            Address user_base = args.address.virtual_addr;
            Address kernel_base = args.address.kernel_addr.virtual_addr;

            TEST_ASSERT_EQUAL(0, argv[4]);
            TEST_ASSERT_EQUAL_STRING("hello_world", ptr(kernel_base + (argv[0] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
        }, error_test_fail());
}

void test_process_alloc_user_pages_maps_and_tracks_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            match_result(UserAddress, Process__alloc_user_pages(process, 123), success(address) {
                    TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, address.virtual_addr);
                    UserAllocation allocation = allocation_at(process, 3);
                    TEST_ASSERT_EQUAL(4, process->user_allocations.len);
                    TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, allocation.type);
                    TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                    TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                        USER_ALLOC_BASE + PAGE_SIZE,
                        true,
                        true,
                        false,
                        PAGE_SIZE));
                }, error_test_fail());
        }, error_test_fail());
}

void test_process_device_address_resolves_each_kernel_virtual_page_to_physical(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            match_result(UserAddress, Process__alloc_user_pages(process, PAGE_SIZE * 2), success(address) {
                    Address second_kernel_page = address.kernel_addr.virtual_addr + PAGE_SIZE;
                    Address second_physical_page = 0x12345000;
                    KAllocStub__set_virtual_translation(second_kernel_page, PAGE_SIZE, second_physical_page);

                    match_result(Address, Process__device_address(process, address.virtual_addr + PAGE_SIZE + 0x80), success(physical) { TEST_ASSERT_EQUAL(second_physical_page + 0x80, physical); }, error_test_fail());
                }, error_test_fail());
        }, error_test_fail());
}

void test_process_free_user_pages_unmaps_and_removes_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            UserAddress address = alloc_user_pages_for_test(process, PAGE_SIZE);

            match_result(ProcessMemoryCode, Process__free_user_pages(process, address.virtual_addr), success(code) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code); }, error_test_fail());

            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_ALLOC_BASE + PAGE_SIZE,
                true,
                true,
                false,
                PAGE_SIZE));
        }, error_test_fail());
}

void test_process_free_user_pages_rejects_static_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            match_result(ProcessMemoryCode, Process__free_user_pages(process, BASE_STACK), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code); });
        }, error_test_fail());
}

void test_process_dispose_user_allocations_unmaps_all_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Process__alloc_user_pages(process, PAGE_SIZE);

            match_result(ProcessMemoryCode, Process__dispose_user_allocations(process), success_do_nothing(), error_test_fail());

            TEST_ASSERT_EQUAL(0, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                0x400000,
                true,
                false,
                true,
                101));
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                BASE_STACK,
                true,
                true,
                false,
                STACK_SIZE));
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_ALLOC_BASE,
                true,
                true,
                false,
                PAGE_SIZE));
        }, error_test_fail());
}

void test_process_dispose_user_allocations_unmaps_mmio_without_freeing_heap(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Address physical = 0x0a000000;
            match_result(UserAddress, Process__map_mmio(process, physical, PAGE_SIZE), success(mmio) {
                    match_optional(UserAllocation, UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1), some(allocation) {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(mmio.virtual_addr, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(physical, allocation.address.kernel_addr.virtual_addr);
                        }, none_test_fail());

                    TEST_ASSERT_TRUE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        PAGE_SIZE));

                    match_result(ProcessMemoryCode, Process__dispose_user_allocations(process), success(code) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code); }, error_test_fail());

                    TEST_ASSERT_EQUAL(0, process->user_allocations.len);
                    TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        PAGE_SIZE));
                }, error_test_fail());
        }, error_test_fail());
}

void test_process_map_mmio_returns_pointer_at_requested_physical_offset(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Address physical = 0x0a000200;
            match_result(UserAddress, Process__map_mmio(process, physical, 0x200), success(mmio) {
                    Address expected_base = USER_ALLOC_BASE + PAGE_SIZE;
                    TEST_ASSERT_EQUAL(expected_base + 0x200, mmio.virtual_addr);
                    TEST_ASSERT_EQUAL(physical, mmio.kernel_addr.virtual_addr);

                    match_optional(UserAllocation, UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1), some(allocation) {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(expected_base, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(0x0a000000, allocation.address.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                        }, none_test_fail());

                    TEST_ASSERT_TRUE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        0x200));
                }, error_test_fail());
        }, error_test_fail());
}

void test_process_sbrk_maps_pages_at_heap_break(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            match_result(Address, Process__sbrk(process, 123), success(old_break) { TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break); }, error_test_fail());

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 3);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_HEAP_BASE,
                true,
                true,
                false,
                PAGE_SIZE));
        }, error_test_fail());
}

void test_process_sbrk_growth_inside_mapped_page_does_not_map_more_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Process__sbrk(process, 123);

            match_result(Address, Process__sbrk(process, 100), success(old_break) { TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, old_break); }, error_test_fail());

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 223, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);
        }, error_test_fail());
}

void test_process_sbrk_growth_across_page_maps_from_previous_mapped_end(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Process__sbrk(process, 123);
            Process__sbrk(process, PAGE_SIZE);

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE * 2, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(5, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 4);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_HEAP_BASE + PAGE_SIZE,
                true,
                true,
                false,
                PAGE_SIZE));
        }, error_test_fail());
}

void test_process_sbrk_can_shrink_without_unmapping_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            Process__sbrk(process, PAGE_SIZE);

            match_result(Address, Process__sbrk(process, -100), success(old_break) { TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, old_break); }, error_test_fail());

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE - 100, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
        }, error_test_fail());
}

void test_process_sbrk_rejects_shrink_below_heap_base(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            match_result(Address, Process__sbrk(process, -1), success_test_fail(), error(code, message) { TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code); });
        }, error_test_fail());
}

void test_process_file_descriptors_allocate_from_three(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    match_result(FileDescriptor, Process__allocate_file_descriptor(process, 0x1234), success(fd) { TEST_ASSERT_EQUAL(3, fd); }, error_test_fail());

    match_result(ProcessIOHandle, Process__get_io_handle(process, 3), success(handle) { TEST_ASSERT_EQUAL(0x1234, handle); }, error_test_fail());
}

void test_process_setup_defaults_to_root_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(process) {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, process->group_id);
            TEST_ASSERT_TRUE(Process__is_root(process));
            TEST_ASSERT_TRUE(Process__has_capability(process, PROCESS_CAP_MAP_MMIO));
        }, error_test_fail());
}

void test_process_child_inherits_parent_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)), success(parent) {
            Process__set_hund_identity(parent);

            match_result(ProcessPtr, Process__setup(kernel, elf, Some(ProcessPtr, parent)), success(child) {
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, child->user_id);
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, child->group_id);
                    TEST_ASSERT_FALSE(Process__is_root(child));
                    TEST_ASSERT_TRUE(Process__has_capability(child, PROCESS_CAP_REGISTER_BLOCK_DEVICE));
                }, error_test_fail());
        }, error_test_fail());
}

void test_process_file_descriptors_can_assign_stdio(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    Process__set_file_descriptor(process, 0, 0x10);
    Process__set_file_descriptor(process, 1, 0x11);
    Process__set_file_descriptor(process, 2, 0x12);

    TEST_ASSERT_EQUAL(3, process->next_file_descriptor);
    TEST_ASSERT_EQUAL(3, process->file_descriptors.len);

    match_result(ProcessIOHandle, Process__get_io_handle(process, 1), success(handle) { TEST_ASSERT_EQUAL(0x11, handle); }, error_test_fail());
}

void test_process_file_descriptors_inherit_from_parent(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    Process__set_file_descriptor(parent, 0, 0x10);
    Process__set_file_descriptor(parent, 1, 0x11);
    Process__set_file_descriptor(parent, 2, 0x12);
    Process__allocate_file_descriptor(parent, 0x1234);

    ProcessPtr child = dummy_process_with_parent(2, 0, PROC_STATE_RUNNING, parent);

    TEST_ASSERT_EQUAL(parent->next_file_descriptor, child->next_file_descriptor);
    TEST_ASSERT_EQUAL(parent->file_descriptors.len, child->file_descriptors.len);

    match_result(ProcessIOHandle, Process__get_io_handle(child, 1), success(handle) { TEST_ASSERT_EQUAL(0x11, handle); }, error_test_fail());

    match_result(FileDescriptor, Process__allocate_file_descriptor(child, 0x5678), success(fd) { TEST_ASSERT_EQUAL(4, fd); }, error_test_fail());
}

void test_process_can_copy_file_descriptor_from_parent(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessPtr child = dummy_process(2, 0, PROC_STATE_RUNNING);
    Process__set_file_descriptor(parent, 7, 0x7000);

    match_result(FileDescriptor, Process__copy_file_descriptor(child, parent, 0, 7), success(fd) { TEST_ASSERT_EQUAL(0, fd); }, error_test_fail());

    match_result(ProcessIOHandle, Process__get_io_handle(child, 0), success(handle) { TEST_ASSERT_EQUAL(0x7000, handle); }, error_test_fail());
}

void test_process_mark_parked_operation_ready_matches_operation_key(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext first_context = KernelContext__new(None(ProcessID), None(SchedulerPtr));
    KernelContext second_context = KernelContext__new(None(ProcessID), None(SchedulerPtr));
    ParkedOperationPtr first = ParkedOperation__heap_new(&first_context, None(Continuation), PARK_WAIT_IO);
    ParkedOperationPtr second = ParkedOperation__heap_new(&second_context, None(Continuation), PARK_WAIT_IO);
    first->operation_key = Some(OperationKey, 10);
    second->operation_key = Some(OperationKey, 20);

    match_result(CollectionCode, Process__push_parked_operation(process, first), success_do_nothing(), error_test_fail());
    match_result(CollectionCode, Process__push_parked_operation(process, second), success_do_nothing(), error_test_fail());

    TEST_ASSERT_TRUE(Process__mark_parked_operation_ready(
        process, PARK_WAIT_IO, Some(OperationKey, 20), Some(ReturnCode, 123)
    ));

    match_optional(ParkedOperationPtr, Process__pop_ready_parked_operation(process), some(ready) {
            TEST_ASSERT_EQUAL_PTR(second, ready);
            ParkedOperation__dispose(ready);
        }, none_test_fail());

    match_optional(ParkedOperationPtr, Process__pop_ready_parked_operation(process), some(ready) {
            ParkedOperation__dispose(ready);
            TEST_FAIL();
        }, none_do_nothing());

    match_optional(ParkedOperationPtr, Process__pop_parked_operation(process), some(remaining) {
            TEST_ASSERT_EQUAL_PTR(first, remaining);
            ParkedOperation__dispose(remaining);
        }, none_test_fail());
}

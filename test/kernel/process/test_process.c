#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../../../common/services/service_directory.h"

#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../stubs/process.h"
#include "../stubs/process_context_layout.h"

static KernelPtr kernel;

static Elf load_hello_world_elf(void)
{
    match_optional(
        EmbeddedElf__load(&bstr("hello_world")),
        some(elf)
        {
            return elf;
        },
        none()
        {
            TEST_FAIL();
            return (Elf){};
        }
    );
}

static UserAllocation allocation_at(ProcessPtr process, uint64_t index)
{
    match_optional(
        UserAllocations__at(&process->user_allocations, index),
        some(allocation)
        {
            return allocation;
        },
        none()
        {
            TEST_FAIL();
            return (UserAllocation){};
        }
    );
}

static UserAddress alloc_user_pages_for_test(ProcessPtr process, size_t size)
{
    match_result(
        Process__alloc_user_pages(process, size),
        success(address)
        {
            return address;
        },
        error(code, message)
        {
            TEST_FAIL();
            return (UserAddress){};
        }
    );
}

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
    KAllocStub__reset_virtual_translation();
    KAllocStub__reset_allocation_tracking();
    kernel = init_test_kernel();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_process_store_context_copies_saved_frame(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessContext source = {
        .x = {0x1122},
        .sp = 0x3344,
        .elr = 0x5566,
        .spsr = 0x7788,
        .fpcr = 0x99aa,
        .fpsr = 0xbbcc,
    };

    match_result(
        Process__store_context(process, &source),
        success(code)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(source.x[0], Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(source.sp, Process__required_context(process)->sp);
    TEST_ASSERT_EQUAL_UINT64(source.elr, Process__required_context(process)->elr);
    TEST_ASSERT_EQUAL_UINT64(source.spsr, Process__required_context(process)->spsr);
    TEST_ASSERT_EQUAL_UINT64(source.fpcr, Process__required_context(process)->fpcr);
    TEST_ASSERT_EQUAL_UINT64(source.fpsr, Process__required_context(process)->fpsr);
}

void test_process_store_context_returns_error_when_process_is_not_running(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    ProcessContext source = {.elr = 0x5566};
    uint64_t original_elr = Process__required_context(process)->elr;

    match_result(
        Process__store_context(process, &source),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_INVALID_STATE, code);
            TEST_ASSERT_EQUAL_BSTRING(bstr("Process not running state=PROC_STATE_WAITING pid=16 name=hello_world\n"),
                                      message);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(original_elr, Process__required_context(process)->elr);
}

void test_process_save_context_without_a_trap_frame_does_not_change_context(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    uint64_t original_elr = Process__required_context(process)->elr;

    match_result(
        Process__save_context(process, &None(ProcessContextPtr)),
        success(code)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(original_elr, Process__required_context(process)->elr);
}

void test_setup_from_elf(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT_EQUAL(Elf__read_header(&elf)->entry, process->entry_point.virtual_addr);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), 0x400000, true, false,
                                             true, PAGE_SIZE));
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), BASE_STACK, true, true,
                                             false, STACK_SIZE));
        },
        error_test_fail()
    );
}

void test_setup_maps_service_directory(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                             HUND_SERVICE_DIRECTORY_VADDR, true, false, false,
                                             HUND_SERVICE_DIRECTORY_SIZE));
        },
        error_test_fail()
    );
}

void test_setup_tracks_static_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
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
        },
        error_test_fail()
    );
}

void test_setup_with_args_sets_up_multiple_argv_entries(void)
{
    Elf elf = load_hello_world_elf();
    ProcessArguments arguments = ProcessArguments__new();
    ProcessArguments__push(&arguments, bstring("one"));
    ProcessArguments__push(&arguments, bstring("two"));
    ProcessArguments__push(&arguments, bstring("three"));

    match_result(
        Process__setup_with_args(kernel, &elf, &None(ProcessPtr), &arguments),
        success(process)
        {
            UserAllocation args = allocation_at(process, 0);
            AddressPtr argv = ptr(args.address.kernel_addr.virtual_addr, Address);
            Address user_base = args.address.virtual_addr;
            Address kernel_base = args.address.kernel_addr.virtual_addr;

            TEST_ASSERT_EQUAL(0, argv[4]);
            TEST_ASSERT_EQUAL_STRING("hello_world", ptr(kernel_base + (argv[0] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_maps_and_tracks_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__alloc_user_pages(process, 123),
                success(address)
                {
                    TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, address.virtual_addr);
                    UserAllocation allocation = allocation_at(process, 3);
                    TEST_ASSERT_EQUAL(4, process->user_allocations.len);
                    TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, allocation.type);
                    TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                    TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                     USER_ALLOC_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_returns_memory_full_when_user_allocation_fails(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            KAllocStub__set_user_alloc_failure(true);
            match_result(
                Process__alloc_user_pages(process, 123),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                    TEST_ASSERT_EQUAL(3, UserAllocations__len(&process->user_allocations));
                }
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_maps_source_pages_into_target_process(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    match_result(
                        Process__map_shared_user_pages(target, source, source_address.virtual_addr, PAGE_SIZE * 2),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr, shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr, shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE * 2, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_preserves_offset_inside_source_page(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE);
                    Address source_offset_address = source_address.virtual_addr + 128;

                    match_result(
                        Process__map_shared_user_pages(target, source, source_offset_address, 256),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + 128, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr + 128,
                                              shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr + 128, shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_maps_offset_range_spanning_next_page(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    Address source_offset_address = source_address.virtual_addr + PAGE_SIZE - 16;

                    match_result(
                        Process__map_shared_user_pages(target, source, source_offset_address, 64),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + PAGE_SIZE - 16, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr + PAGE_SIZE - 16,
                                              shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr + PAGE_SIZE - 16,
                                              shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE * 2, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_rejects_unallocated_source_range(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    match_result(
                        Process__map_shared_user_pages(target, source, 0x900000000, PAGE_SIZE),
                        success_test_fail(),
                        error(code, message)
                        {
                            TEST_ASSERT_EQUAL(PROCESS_MEMORY_NOT_FOUND, code);
                        }
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_unmaps_shared_allocation_without_freeing_source(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE);

                    match_result(
                        Process__map_shared_user_pages(target, source, source_address.virtual_addr, PAGE_SIZE),
                        success(shared)
                        {
                            match_result(
                                Process__free_user_pages(target, shared.virtual_addr),
                                success(status)
                                {
                                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, status);
                                },
                                error_test_fail()
                            );

                            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                                   shared.virtual_addr, true, true, false, PAGE_SIZE));
                            match_result(
                                Process__device_address(source, source_address.virtual_addr),
                                success(physical)
                                {
                                    TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr, physical);
                                },
                                error_test_fail()
                            );
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_with_allocation_returns_mapping_and_allocation_base(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    Address source_offset_address = source_address.virtual_addr + 123;

                    match_result(
                        Process__map_shared_user_pages_with_allocation(target, source, source_offset_address,
                                                                       PAGE_SIZE),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, shared.allocation.virtual_addr);
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + 123, shared.mapping.virtual_addr);

                            match_result(
                                Process__free_user_pages(target, shared.allocation.virtual_addr),
                                success(status)
                                {
                                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, status);
                                },
                                error_test_fail()
                            );

                            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                                   shared.allocation.virtual_addr, true, true, false,
                                                                   PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_device_address_resolves_each_kernel_virtual_page_to_physical(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__alloc_user_pages(process, PAGE_SIZE * 2),
                success(address)
                {
                    Address second_kernel_page = address.kernel_addr.virtual_addr + PAGE_SIZE;
                    Address second_physical_page = 0x12345000;
                    KAllocStub__set_virtual_translation(second_kernel_page, PAGE_SIZE, second_physical_page);

                    match_result(
                        Process__device_address(process, address.virtual_addr + PAGE_SIZE + 0x80),
                        success(physical)
                        {
                            TEST_ASSERT_EQUAL(second_physical_page + 0x80, physical);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_unmaps_and_removes_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            UserAddress address = alloc_user_pages_for_test(process, PAGE_SIZE);

            match_result(
                Process__free_user_pages(process, address.virtual_addr),
                success(code)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                   USER_ALLOC_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_rejects_static_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__free_user_pages(process, BASE_STACK),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_dispose_user_allocations_unmaps_all_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__alloc_user_pages(process, PAGE_SIZE);

            match_result(Process__dispose_user_allocations(process),
                success_do_nothing(),
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(0, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), 0x400000, true,
                                                   false, true, 101));
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), BASE_STACK, true,
                                                   true, false, STACK_SIZE));
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), USER_ALLOC_BASE,
                                                   true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_dispose_page_table_frees_owned_pages_and_tracking_nodes(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            size_t page_count = ProcessPageTable__page_count(&process->page_table);
            size_t free_count_before = KAllocStub__free_count();
            size_t mappings_before = MemoryMappings__len(&kernel->memory_accounting.mappings);
            Address space = ProcessPageTable__root(&process->page_table).virtual_addr;

            match_result(Process__dispose_page_table(process),
                success_do_nothing(),
                error_test_fail()
            );

            size_t removed_mappings = mappings_before - MemoryMappings__len(&kernel->memory_accounting.mappings);
            TEST_ASSERT_EQUAL(free_count_before + (page_count * 2) + removed_mappings, KAllocStub__free_count());
            match_optional(
                MemoryAccounting__usage(&kernel->memory_accounting, space),
                some(usage)
                {
                    TEST_ASSERT_EQUAL_UINT64(0, usage.private_bytes);
                    TEST_ASSERT_EQUAL_UINT64(0, usage.shared_bytes);
                    TEST_ASSERT_EQUAL_UINT64(0, usage.device_bytes);
                },
                none_test_fail()
            );
            TEST_ASSERT_EQUAL(0, ProcessPageTable__page_count(&process->page_table));
            TEST_ASSERT_FALSE(ProcessPageTable__root(&process->page_table).valid);
        },
        error_test_fail()
    );
}

void test_process_claimed_device_is_reset_before_memory_disposal(void)
{
    extern Address test_reset_device;
    test_reset_device = 0;
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x0a000000, PAGE_SIZE),
        success(mmio)
        {
            match_result(Process__claim_virtio_device(process, mmio.virtual_addr),
                success_do_nothing(),
                         error_test_fail()
            );
            match_result(Process__dispose_user_allocations(process),
                success_do_nothing(),
                error_test_fail()
            );
            TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr, test_reset_device);
            match_optional(process->virtio_device,
                some_test_fail(),
                none_do_nothing()
            );
            test_reset_device = 0;
            Process__quiesce_device(process);
            TEST_ASSERT_EQUAL_UINT64(0, test_reset_device);
        },
        error_test_fail()
    );
}

void test_process_dispose_user_allocations_unmaps_mmio_without_freeing_heap(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Address physical = 0x0a000000;
            match_result(
                Process__map_mmio(process, physical, PAGE_SIZE),
                success(mmio)
                {
                    match_optional(
                        UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1),
                        some(allocation)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(mmio.virtual_addr, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(physical, allocation.address.kernel_addr.virtual_addr);
                        },
                        none_test_fail()
                    );

                    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                          mmio.virtual_addr, true, true, false, PAGE_SIZE));

                    match_result(
                        Process__dispose_user_allocations(process),
                        success(code)
                        {
                            TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code);
                        },
                        error_test_fail()
                    );

                    TEST_ASSERT_EQUAL(0, process->user_allocations.len);
                    TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                           mmio.virtual_addr, true, true, false, PAGE_SIZE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_mmio_returns_pointer_at_requested_physical_offset(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Address physical = 0x0a000200;
            match_result(
                Process__map_mmio(process, physical, 0x200),
                success(mmio)
                {
                    Address expected_base = USER_ALLOC_BASE + PAGE_SIZE;
                    TEST_ASSERT_EQUAL(expected_base + 0x200, mmio.virtual_addr);
                    TEST_ASSERT_EQUAL(physical, mmio.kernel_addr.virtual_addr);

                    match_optional(
                        UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1),
                        some(allocation)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(expected_base, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(0x0a000000, allocation.address.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                        },
                        none_test_fail()
                    );

                    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                          mmio.virtual_addr, true, true, false, 0x200));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_sbrk_maps_pages_at_heap_break(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__sbrk(process, 123),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 3);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), USER_HEAP_BASE, true,
                                             true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_sbrk_growth_inside_mapped_page_does_not_map_more_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, 123);

            match_result(
                Process__sbrk(process, 100),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 223, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);
        },
        error_test_fail()
    );
}

void test_process_sbrk_growth_across_page_maps_from_previous_mapped_end(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, 123);
            Process__sbrk(process, PAGE_SIZE);

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE * 2, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(5, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 4);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                             USER_HEAP_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_sbrk_can_shrink_without_unmapping_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, PAGE_SIZE);

            match_result(
                Process__sbrk(process, -100),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE - 100, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
        },
        error_test_fail()
    );
}

void test_process_sbrk_rejects_shrink_below_heap_base(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__sbrk(process, -1),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_allocations_cannot_grow_into_service_or_shared_page_regions(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            process->allocation_next = HUND_SERVICE_DIRECTORY_VADDR;
            match_result(
                Process__alloc_user_pages(process, PAGE_SIZE),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
            match_result(
                Process__map_mmio(process, 0x1000, PAGE_SIZE),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
            match_result(
                Process__sbrk(process, USER_ALLOC_BASE - USER_HEAP_BASE + 1),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_sbrk_returns_memory_full_when_user_allocation_fails(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            KAllocStub__set_user_alloc_failure(true);
            match_result(
                Process__sbrk(process, 123),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, process->heap_break);
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, process->heap_mapped_end);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_setup_defaults_to_root_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, process->group_id);
            TEST_ASSERT_TRUE(Process__is_root(process));
            TEST_ASSERT_TRUE(Process__has_capability(process, PROCESS_CAP_MAP_MMIO));
        },
        error_test_fail()
    );
}

void test_process_child_inherits_parent_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(parent)
        {
            Process__set_hund_identity(parent);

            match_result(
                Process__setup(kernel, &elf, &Some(ProcessPtr, parent)),
                success(child)
                {
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, child->user_id);
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, child->group_id);
                    TEST_ASSERT_FALSE(Process__is_root(child));
                    TEST_ASSERT_TRUE(Process__has_capability(child, PROCESS_CAP_REGISTER_BLOCK_DEVICE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_mark_parked_operation_ready_matches_operation_key(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext first_context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    KernelContext second_context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    ParkedOperationPtr first = ParkedOperation__heap_new(&first_context, &None(Continuation), PARK_WAIT_IO);
    ParkedOperationPtr second = ParkedOperation__heap_new(&second_context, &None(Continuation), PARK_WAIT_IO);
    first->operation_key = Some(OperationKey, 10);
    second->operation_key = Some(OperationKey, 20);

    match_result(Process__push_parked_operation(process, first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__push_parked_operation(process, second),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_TRUE(
        Process__mark_parked_operation_ready(process, PARK_WAIT_IO, &Some(OperationKey, 20), &Some(ReturnCode, 123)));
    TEST_ASSERT_FALSE(Process__has_ready_parked_operation(process, PARK_WAIT_IO, &Some(OperationKey, 10)));
    TEST_ASSERT_TRUE(Process__has_ready_parked_operation(process, PARK_WAIT_IO, &Some(OperationKey, 20)));

    match_optional(
        Process__pop_ready_parked_operation(process),
        some(ready)
        {
            TEST_ASSERT_EQUAL_PTR(second, ready);
            ParkedOperation__dispose(ready);
        },
        none_test_fail()
    );

    match_optional(
        Process__pop_ready_parked_operation(process),
        some(ready)
        {
            ParkedOperation__dispose(ready);
            TEST_FAIL();
        },
        none_do_nothing()
    );

    match_optional(
        Process__pop_parked_operation(process),
        some(remaining)
        {
            TEST_ASSERT_EQUAL_PTR(first, remaining);
            ParkedOperation__dispose(remaining);
        },
        none_test_fail()
    );
}

void test_process_wake_expired_sleep_ignores_process_that_is_not_sleeping(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    ParkedOperationPtr sleep = ParkedOperation__heap_new(&context, &None(Continuation), PARK_SLEEP);
    sleep->operation_key = Some(OperationKey, 1);

    match_result(Process__push_parked_operation(process, sleep),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_FALSE(Process__wake_expired_sleep(process, 2));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);
    TEST_ASSERT_FALSE(ParkedOperation__is_ready(sleep));

    match_optional(
        Process__pop_parked_operation(process),
        some(remaining)
        {
            TEST_ASSERT_EQUAL_PTR(sleep, remaining);
            ParkedOperation__dispose(remaining);
        },
        none_test_fail()
    );
}

void test_terminal_endpoints_inherit_without_kernel_io_resources(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    parent->startup_stdio =
        SpawnStdio__new(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    ProcessPtr child = dummy_process_with_parent(2, 0, PROC_STATE_READY, parent);
    Process__prepare_startup_stdio(child);
    TestStartupStdio stdio = startup_stdio();
    TEST_ASSERT_EQUAL_UINT64(child->pid, stdio.pid);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_TTY_INPUT, stdio.input);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, stdio.output);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, stdio.error);
}

void test_spawn_stdio_resolves_standard_endpoints_and_rejects_general_handles(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    parent->startup_stdio =
        SpawnStdio__new(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    SpawnStdio request = SpawnStdio__new(IO_ENDPOINT_KEYBOARD_INPUT, 2, 1);
    match_result(
        Process__stdio_for_spawn(parent, &request),
        success(stdio)
        {
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_KEYBOARD_INPUT, SpawnStdio__stdin_fd(stdio));
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stdout_fd(stdio));
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stderr_fd(stdio));
        },
        error_test_fail()
    );
    request = SpawnStdio__new(7, 1, 2);
    match_result(
        Process__stdio_for_spawn(parent, &request),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
}

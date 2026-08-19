#include "unity.h"
#include "../../../kernel/process.h"
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(process,
        {
            TEST_ASSERT_EQUAL(Elf__read_header(elf)->entry, process->entry_point.virtual_addr);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                0x400000,
                true,
                false,
                true,
                101));
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                BASE_STACK,
                true,
                true,
                false,
                STACK_SIZE));
        })
    End
}

void test_setup_tracks_static_user_allocations(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            Optional(UserAllocation) args_allocation = UserAllocations__at(&process->user_allocations, 0);
            Optional(UserAllocation) elf_allocation = UserAllocations__at(&process->user_allocations, 1);
            Optional(UserAllocation) stack_allocation = UserAllocations__at(&process->user_allocations, 2);
            UserAllocation args = Unwrap(UserAllocation, args_allocation, (UserAllocation){});
            UserAllocation elf = Unwrap(UserAllocation, elf_allocation, (UserAllocation){});
            UserAllocation stack = Unwrap(UserAllocation, stack_allocation, (UserAllocation){});
            TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, args.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, args.size);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_ELF, elf.type);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_STACK, stack.type);
        })
    End
}

void test_setup_with_args_sets_up_multiple_argv_entries(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ProcessArguments arguments = ProcessArguments__new();
    ProcessArguments__push(&arguments, bstring("one"));
    ProcessArguments__push(&arguments, bstring("two"));
    ProcessArguments__push(&arguments, bstring("three"));

    ResultMatch(ProcessPtr, Process__setup_with_args(kernel, elf, None(ProcessPtr), arguments))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            UserAllocation args = Unwrap(UserAllocation, UserAllocations__at(&process->user_allocations, 0), (UserAllocation){});
            Address * argv = ptr(args.address.kernel_addr.virtual_addr, Address);
            Address user_base = args.address.virtual_addr;
            Address kernel_base = args.address.kernel_addr.virtual_addr;

            TEST_ASSERT_EQUAL(0, argv[4]);
            TEST_ASSERT_EQUAL_STRING("hello_world", ptr(kernel_base + (argv[0] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
        })
    End
}

void test_process_alloc_user_pages_maps_and_tracks_dynamic_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(UserAddress, Process__alloc_user_pages(process, 123))
                IsError(code, message, TEST_FAIL())
                IsSuccess(address,
                {
                    TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, address.virtual_addr);
                    UserAllocation allocation = Unwrap(
                        UserAllocation, UserAllocations__at(&process->user_allocations, 3), (UserAllocation) {}
                    );
                    TEST_ASSERT_EQUAL(4, process->user_allocations.len);
                    TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, allocation.type);
                    TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                    TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                        USER_ALLOC_BASE + PAGE_SIZE,
                        true,
                        true,
                        false,
                        PAGE_SIZE));
                })
            End
        })
    End
}

void test_process_device_address_resolves_each_kernel_virtual_page_to_physical(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(UserAddress, Process__alloc_user_pages(process, PAGE_SIZE * 2))
                IsError(code, message, TEST_FAIL())
                IsSuccess(address,
                {
                    Address second_kernel_page = address.kernel_addr.virtual_addr + PAGE_SIZE;
                    Address second_physical_page = 0x12345000;
                    KAllocStub__set_virtual_translation(second_kernel_page, PAGE_SIZE, second_physical_page);

                    ResultMatch(Address, Process__device_address(process, address.virtual_addr + PAGE_SIZE + 0x80))
                        IsError(code, message, TEST_FAIL())
                        IsSuccess(physical, TEST_ASSERT_EQUAL(second_physical_page + 0x80, physical))
                    End
                })
            End
        })
    End
}

void test_process_free_user_pages_unmaps_and_removes_dynamic_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Result(UserAddress) alloc_result = Process__alloc_user_pages(process, PAGE_SIZE);
            UserAddress address = ResultUnwrap(UserAddress, alloc_result, (UserAddress){});

            ResultMatch(ProcessMemoryCode, Process__free_user_pages(process, address.virtual_addr))
                IsSuccess(code, TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code))
                IsError(code, message, TEST_FAIL())
            End

            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_ALLOC_BASE + PAGE_SIZE,
                true,
                true,
                false,
                PAGE_SIZE));
        })
    End
}

void test_process_free_user_pages_rejects_static_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(ProcessMemoryCode, Process__free_user_pages(process, BASE_STACK))
                IsSuccess(code, TEST_FAIL())
                IsError(code, message, TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code))
            End
        })
    End
}

void test_process_dispose_user_allocations_unmaps_all_user_allocations(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Process__alloc_user_pages(process, PAGE_SIZE);

            ResultMatch(ProcessMemoryCode, Process__dispose_user_allocations(process))
                IsError(code, message, TEST_FAIL())
            End

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
        })
    End
}

void test_process_dispose_user_allocations_unmaps_mmio_without_freeing_heap(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Address physical = 0x0a000000;
            ResultMatch(UserAddress, Process__map_mmio(process, physical, PAGE_SIZE))
                IsSuccess(mmio,
                {
                    Match(UserAllocation, UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1))
                        IsSome(allocation,
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(mmio.virtual_addr, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(physical, allocation.address.kernel_addr.virtual_addr);
                        })
                        IsNone(TEST_FAIL())
                    End

                    TEST_ASSERT_TRUE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        PAGE_SIZE));

                    ResultMatch(ProcessMemoryCode, Process__dispose_user_allocations(process))
                        IsSuccess(code, TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code))
                        IsError(code, message, TEST_FAIL())
                    End

                    TEST_ASSERT_EQUAL(0, process->user_allocations.len);
                    TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        PAGE_SIZE));
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_process_map_mmio_returns_pointer_at_requested_physical_offset(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Address physical = 0x0a000200;
            ResultMatch(UserAddress, Process__map_mmio(process, physical, 0x200))
                IsSuccess(mmio,
                {
                    Address expected_base = USER_ALLOC_BASE + PAGE_SIZE;
                    TEST_ASSERT_EQUAL(expected_base + 0x200, mmio.virtual_addr);
                    TEST_ASSERT_EQUAL(physical, mmio.kernel_addr.virtual_addr);

                    Match(UserAllocation, UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1))
                        IsSome(allocation,
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(expected_base, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(0x0a000000, allocation.address.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                        })
                        IsNone(TEST_FAIL())
                    End

                    TEST_ASSERT_TRUE(check_address_mapped(v_ptr(process->page_table, PageTable),
                        mmio.virtual_addr,
                        true,
                        true,
                        false,
                        0x200));
                })
                IsError(code, message, TEST_FAIL())
            End
        })
    End
}

void test_process_sbrk_maps_pages_at_heap_break(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(Address, Process__sbrk(process, 123))
                IsError(code, message, TEST_FAIL())
                IsSuccess(old_break, TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break))
            End

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);

            UserAllocation allocation = Unwrap(
                UserAllocation, UserAllocations__at(&process->user_allocations, 3), (UserAllocation) {}
            );
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_HEAP_BASE,
                true,
                true,
                false,
                PAGE_SIZE));
        })
    End
}

void test_process_sbrk_growth_inside_mapped_page_does_not_map_more_pages(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Process__sbrk(process, 123);

            ResultMatch(Address, Process__sbrk(process, 100))
                IsError(code, message, TEST_FAIL())
                IsSuccess(old_break, TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, old_break))
            End

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 223, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);
        })
    End
}

void test_process_sbrk_growth_across_page_maps_from_previous_mapped_end(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Process__sbrk(process, 123);
            Process__sbrk(process, PAGE_SIZE);

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE * 2, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(5, process->user_allocations.len);

            UserAllocation allocation = Unwrap(
                UserAllocation, UserAllocations__at(&process->user_allocations, 4), (UserAllocation) {}
            );
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_HEAP_BASE + PAGE_SIZE,
                true,
                true,
                false,
                PAGE_SIZE));
        })
    End
}

void test_process_sbrk_can_shrink_without_unmapping_pages(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Process__sbrk(process, PAGE_SIZE);

            ResultMatch(Address, Process__sbrk(process, -100))
                IsError(code, message, TEST_FAIL())
                IsSuccess(old_break, TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, old_break))
            End

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE - 100, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
        })
    End
}

void test_process_sbrk_rejects_shrink_below_heap_base(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(Address, Process__sbrk(process, -1))
                IsSuccess(old_break, TEST_FAIL())
                IsError(code, message, TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code))
            End
        })
    End
}

void test_process_file_descriptors_allocate_from_three(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    ResultMatch(FileDescriptor, Process__allocate_file_descriptor(process, 0x1234))
        IsSuccess(fd, TEST_ASSERT_EQUAL(3, fd))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ProcessIOHandle, Process__get_io_handle(process, 3))
        IsSuccess(handle, TEST_ASSERT_EQUAL(0x1234, handle))
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_setup_defaults_to_root_identity(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, process->group_id);
            TEST_ASSERT_TRUE(Process__is_root(process));
            TEST_ASSERT_TRUE(Process__has_capability(process, PROCESS_CAP_MAP_MMIO));
        })
    End
}

void test_process_child_inherits_parent_identity(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(bstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(kernel, elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(parent,
        {
            Process__set_hund_identity(parent);

            ResultMatch(ProcessPtr, Process__setup(kernel, elf, Some(ProcessPtr, parent)))
                IsError(code, message, TEST_FAIL())
                IsSuccess(child,
                {
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, child->user_id);
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, child->group_id);
                    TEST_ASSERT_FALSE(Process__is_root(child));
                    TEST_ASSERT_TRUE(Process__has_capability(child, PROCESS_CAP_REGISTER_BLOCK_DEVICE));
                })
            End
        })
    End
}

void test_process_file_descriptors_can_assign_stdio(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    Process__set_file_descriptor(process, 0, 0x10);
    Process__set_file_descriptor(process, 1, 0x11);
    Process__set_file_descriptor(process, 2, 0x12);

    TEST_ASSERT_EQUAL(3, process->next_file_descriptor);
    TEST_ASSERT_EQUAL(3, process->file_descriptors.len);

    ResultMatch(ProcessIOHandle, Process__get_io_handle(process, 1))
        IsSuccess(handle, TEST_ASSERT_EQUAL(0x11, handle))
        IsError(code, message, TEST_FAIL())
    End
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

    ResultMatch(ProcessIOHandle, Process__get_io_handle(child, 1))
        IsSuccess(handle, TEST_ASSERT_EQUAL(0x11, handle))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(FileDescriptor, Process__allocate_file_descriptor(child, 0x5678))
        IsSuccess(fd, TEST_ASSERT_EQUAL(4, fd))
        IsError(code, message, TEST_FAIL())
    End
}

void test_process_can_copy_file_descriptor_from_parent(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessPtr child = dummy_process(2, 0, PROC_STATE_RUNNING);
    Process__set_file_descriptor(parent, 7, 0x7000);

    ResultMatch(FileDescriptor, Process__copy_file_descriptor(child, parent, 0, 7))
        IsSuccess(fd, TEST_ASSERT_EQUAL(0, fd))
        IsError(code, message, TEST_FAIL())
    End

    ResultMatch(ProcessIOHandle, Process__get_io_handle(child, 0))
        IsSuccess(handle, TEST_ASSERT_EQUAL(0x7000, handle))
        IsError(code, message, TEST_FAIL())
    End
}

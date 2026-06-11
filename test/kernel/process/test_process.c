#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"

#include "../fixtures/process.h"
#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_setup_from_elf(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(process,
        {
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            TEST_ASSERT_EQUAL(2, process->user_allocations.len);
            Optional(UserAllocation) elf_allocation = UserAllocations__at(&process->user_allocations, 0);
            Optional(UserAllocation) stack_allocation = UserAllocations__at(&process->user_allocations, 1);
            UserAllocation elf = UnwrapPanic(UserAllocation, elf_allocation);
            UserAllocation stack = UnwrapPanic(UserAllocation, stack_allocation);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_ELF, elf.type);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_STACK, stack.type);
        })
    End
}

void test_process_alloc_user_pages_maps_and_tracks_dynamic_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(UserAddress, Process__alloc_user_pages(process, 123))
                IsError(code, message, TEST_FAIL())
                IsSuccess(address,
                {
                    TEST_ASSERT_EQUAL(USER_ALLOC_BASE, address.virtual_addr);
                    UserAllocation allocation = UnwrapPanic(
                        UserAllocation, UserAllocations__at(&process->user_allocations, 2)
                    );
                    TEST_ASSERT_EQUAL(3, process->user_allocations.len);
                    TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, allocation.type);
                    TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                    TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
                        USER_ALLOC_BASE,
                        true,
                        true,
                        false,
                        PAGE_SIZE));
                })
            End
        })
    End
}

void test_process_free_user_pages_unmaps_and_removes_dynamic_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Result(UserAddress) alloc_result = Process__alloc_user_pages(process, PAGE_SIZE);
            UserAddress address = ResultUnwrapPanic(UserAddress, alloc_result);

            ResultMatch(ProcessMemoryCode, Process__free_user_pages(process, address.virtual_addr))
                IsSuccess(code, TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code))
                IsError(code, message, TEST_FAIL())
            End

            TEST_ASSERT_EQUAL(2, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(v_ptr(process->page_table, PageTable),
                USER_ALLOC_BASE,
                true,
                true,
                false,
                PAGE_SIZE));
        })
    End
}

void test_process_free_user_pages_rejects_static_allocation(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
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

void test_process_sbrk_maps_pages_at_heap_break(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            ResultMatch(Address, Process__sbrk(process, 123))
                IsError(code, message, TEST_FAIL())
                IsSuccess(old_break, TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break))
            End

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(3, process->user_allocations.len);

            UserAllocation allocation = UnwrapPanic(
                UserAllocation, UserAllocations__at(&process->user_allocations, 2)
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
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
            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
        })
    End
}

void test_process_sbrk_growth_across_page_maps_from_previous_mapped_end(void)
{
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
        IsError(code, message, TEST_FAIL())
        IsSuccess(process,
        {
            Process__sbrk(process, 123);
            Process__sbrk(process, PAGE_SIZE);

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE * 2, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);

            UserAllocation allocation = UnwrapPanic(
                UserAllocation, UserAllocations__at(&process->user_allocations, 3)
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
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
    Elf elf = Unwrap(Elf, EmbeddedElf__load(kstring("hello_world")), (Elf){});
    ResultMatch(ProcessPtr, Process__setup(elf, None(ProcessPtr)))
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

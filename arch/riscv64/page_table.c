#include <stddef.h>
#include <stdint.h>

#include "page_table.h"

#include "mmu.h"
#include "../../kernel/debug.h"
#include "../../kernel/kalloc.h"
#include "../../kernel/kernel_core.h"

static BString page_table_allocation_failed = bstr("Page table allocation failed");

#define RISCV_SATP_PPN_MASK ((1ULL << 44U) - 1U)

static bool PageTable__copy_supervisor_mappings(PageTablePtr target, KernelPtr kernel)
{
    uint64_t satp;
    __asm__ volatile("csrr %0, satp" : "=r"(satp));
    Address active_root_physical = (satp & RISCV_SATP_PPN_MASK) << 12U;
    KernelAddress active_root = Kernel__from_physical_address(kernel, active_root_physical);
    if (!active_root.valid)
    {
        return false;
    }

    PageTablePtr source = v_ptr(active_root, PageTable);
    for (uint64_t index = RISCV_SV39_USER_ROOT_ENTRIES; index < RISCV_SV39_PAGE_TABLE_ENTRIES; index++)
    {
        target->entries[index] = source->entries[index];
    }
    return true;
}

Result(ProcessPageTable) ProcessPageTable__new(KernelPtr kernel)
{
    KernelAddress table = Kernel__alloc_page();
    if (!table.valid)
    {
        return Error(ProcessPageTable, page_table_allocation_failed, KALLOC_ALLOCATION_FAILED);
    }

    ProcessPageTable process_page_table = (ProcessPageTable){
        .root = table,
        .pages = ProcessPageTablePages__new(),
    };

    if (!PageTable__copy_supervisor_mappings(v_ptr(table, PageTable), kernel))
    {
        match_result(
            Kernel__free(table.virtual_addr),
            success_do_nothing(),
            error(code, message)
            {
                return Error(ProcessPageTable, message, code);
            }
        );
        return Error(ProcessPageTable, page_table_allocation_failed, KALLOC_ALLOCATION_FAILED);
    }

    match_result(
        ProcessPageTable__record_page(&process_page_table, table),
        success_do_nothing(),
        error(code, message)
        {
            match_result(
                Kernel__free(table.virtual_addr),
                success_do_nothing(),
                error(free_code, free_message)
                {
                    return Error(ProcessPageTable, free_message, free_code);
                }
            );
            return Error(ProcessPageTable, message, code);
        }
    );

    __d_printf(MEMORY, "pagetable alloc level=root addr=%p phys=%p size=%i valid=%i\n", table.virtual_addr,
               table.phys_addr, PAGE_SIZE, (int)table.valid);
    return Success(ProcessPageTable, process_page_table);
}

static KernelAddress PageTable__from_physical_address(KernelPtr kernel, Address physical_address)
{
    return Kernel__from_physical_address(kernel, physical_address);
}

static void PageTable__publish(void)
{
    __asm__ volatile("fence rw, rw" ::: "memory");
}

static void PageTable__invalidate_page(Address virtual_address)
{
    __asm__ volatile("sfence.vma %0, zero" : : "r"(virtual_address) : "memory");
}

static KernelAddress PageTable__next_level_table(KernelAddress parent, KernelPtr kernel, uint64_t child_index)
{
    RiscVPageTableEntry entry = v_ptr(parent, PageTable)->entries[child_index];
    if (!RiscVPageTableEntry__is_table(entry))
    {
        return NULL_KERNEL_ADDRESS;
    }
    return PageTable__from_physical_address(kernel, RiscVPageTableEntry__physical_address(entry));
}

static KernelAddress PageTable__ensure_next_level_table(ProcessPageTablePtr process_page_table, KernelAddress parent,
                                                        KernelPtr kernel, uint64_t child_index)
{
    PageTablePtr parent_table = v_ptr(parent, PageTable);
    RiscVPageTableEntry entry = parent_table->entries[child_index];
    if (RiscVPageTableEntry__is_table(entry))
    {
        return PageTable__from_physical_address(kernel, RiscVPageTableEntry__physical_address(entry));
    }
    if ((entry & RISCV_PTE_VALID) != 0)
    {
        return NULL_KERNEL_ADDRESS;
    }

    KernelAddress child = Kernel__alloc_page();
    if (!child.valid)
    {
        return NULL_KERNEL_ADDRESS;
    }

    match_result(
        ProcessPageTable__record_page(process_page_table, child),
        success_do_nothing(),
        error(code, message)
        {
            match_result(
                Kernel__free(child.virtual_addr),
                success_do_nothing(),
                error(free_code, free_message)
                {
                    __d_printf(MEMORY, "pagetable record cleanup failed addr=%p code=%i message=%s\n",
                               child.virtual_addr, free_code, BString__cstr(&free_message));
                }
            );
            return NULL_KERNEL_ADDRESS;
        }
    );

    parent_table->entries[child_index] = RiscVPageTableEntry__table(child.phys_addr);
    PageTable__publish();
    return child;
}

static int PageTable__map_page(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address,
                               Address physical_address, ConstRiscVPagePermissionsPtr permissions)
{
    if (!RiscVVirtualAddress__is_user(virtual_address))
    {
        return -1;
    }

    KernelAddress root = ProcessPageTable__root(self);
    if (!root.valid)
    {
        return -1;
    }

    KernelAddress level_1 =
        PageTable__ensure_next_level_table(self, root, kernel, RISCV_SV39_LEVEL_2_INDEX(virtual_address));
    if (!level_1.valid)
    {
        return -1;
    }

    KernelAddress level_0 =
        PageTable__ensure_next_level_table(self, level_1, kernel, RISCV_SV39_LEVEL_1_INDEX(virtual_address));
    if (!level_0.valid)
    {
        return -1;
    }

    PageTablePtr leaf_table = v_ptr(level_0, PageTable);
    leaf_table->entries[RISCV_SV39_LEVEL_0_INDEX(virtual_address)] =
        RiscVPageTableEntry__leaf(physical_address, permissions);
    PageTable__publish();
    PageTable__invalidate_page(virtual_address);
    return 0;
}

void ArchPageTable__map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    ArchPageTable__try_map(self, kernel, address, size);
}

Result(KAllocCode) ArchPageTable__try_map(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    Address user_start = align_down_4k(vaddr(address));
    Address user_end = align_up_4k(vaddr(address) + size);
    Address kernel_start = align_down_4k(u_kvaddr(address) - (vaddr(address) - user_start));
    RiscVPagePermissions permissions = RiscVPagePermissions__new(address.read, address.write, address.execute, true);

    for (Address user_address = user_start; user_address < user_end; user_address += PAGE_SIZE)
    {
        Address offset = user_address - user_start;
        KernelAddress backing = Kernel__try_from_virtual_address(kernel, kernel_start + offset);
        Address physical_address = align_down_4k(u_kpaddr(address)) + offset;
        if (backing.valid)
        {
            physical_address = align_down_4k(paddr(backing));
        }

        if (PageTable__map_page(self, kernel, user_address, physical_address, &permissions) != 0)
        {
            ArchPageTable__unmap(self, kernel, user_start, offset);
            return Error(KAllocCode, page_table_allocation_failed, KALLOC_ALLOCATION_FAILED);
        }
    }

    return Success(KAllocCode, KALLOC_OK);
}

void ArchPageTable__map_mmio(ProcessPageTablePtr self, KernelPtr kernel, UserAddress address, size_t size)
{
    Address user_start = align_down_4k(vaddr(address));
    Address user_end = align_up_4k(vaddr(address) + size);
    Address physical_start = align_down_4k(u_kpaddr(address));
    RiscVPagePermissions permissions = RiscVPagePermissions__new(true, true, false, true);

    for (Address user_address = user_start; user_address < user_end; user_address += PAGE_SIZE)
    {
        Address offset = user_address - user_start;
        PageTable__map_page(self, kernel, user_address, physical_start + offset, &permissions);
    }
}

void ArchPageTable__unmap(ProcessPageTablePtr self, KernelPtr kernel, Address virtual_address, size_t size)
{
    Address user_start = align_down_4k(virtual_address);
    if (!RiscVVirtualAddress__is_user(user_start))
    {
        return;
    }

    Address user_end = align_up_4k(virtual_address + size);
    if (user_end > RISCV_SV39_USER_ADDRESS_LIMIT)
    {
        user_end = RISCV_SV39_USER_ADDRESS_LIMIT;
    }
    KernelAddress root = ProcessPageTable__root(self);

    for (Address user_address = user_start; user_address < user_end; user_address += PAGE_SIZE)
    {
        KernelAddress level_1 = PageTable__next_level_table(root, kernel, RISCV_SV39_LEVEL_2_INDEX(user_address));
        if (!level_1.valid)
        {
            continue;
        }

        KernelAddress level_0 = PageTable__next_level_table(level_1, kernel, RISCV_SV39_LEVEL_1_INDEX(user_address));
        if (!level_0.valid)
        {
            continue;
        }

        v_ptr(level_0, PageTable)->entries[RISCV_SV39_LEVEL_0_INDEX(user_address)] = 0;
        PageTable__invalidate_page(user_address);
    }
}

#include "process.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/process.h"

#include "debug.h"
#include "kalloc.h"
#include "stack.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(CPUProcesses);


Process * Process__new(KernelAddress page_table, UserAddress entry, Stack stack)
{
    Process * proc = (Process *) Kernel__alloc_kernel(sizeof(Process));
    proc->entry_point = entry;
    proc->page_table = page_table;
    proc->state = PROC_STATE_NEW;
    proc->pid = 0;
    proc->stack = stack;
    return proc;
}

Process * Process__spawn_from_elf(Elf * elf) {
    __d_println(FOCUS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(elf);
    __d_println(FOCUS, "Creating page table");
    KernelAddress page_table = PageTable__new();

    __d_println(FOCUS, "Page table created");
    for (int i = 0; i < expanded_elf.len; i++)
    {
        ExpandedSegment segment = ExpandedSegmentArray__at(&expanded_elf, i);
        PageTable__map(v_ptr(page_table, PageTable), segment.address, segment.size);
    }
    __d_println(FOCUS, "Code mapped");

    Stack stack = Stack__new();
    Stack__map(&stack, v_ptr(page_table, PageTable));
    __d_println(FOCUS, "Stack mapped");

    ExpandedSegment entry_point = ExpandedSegmentArray__at(&expanded_elf, 0);
    Process * proc = Process__new(page_table, entry_point.address, stack);
    __d_println(FOCUS, "Process created");
    Process__start(proc);

    return proc;
}
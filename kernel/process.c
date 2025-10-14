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
    __d_println(DEBUG, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(elf);
    __d_println(DEBUG, "Creating page table");
    KernelAddress page_table = PageTable__new();

    __d_println(DEBUG, "Page table created");
    for (int i = 0; i < expanded_elf.len; i++)
    {
        Optional(ExpandedSegment) segment_optional = ExpandedSegmentArray__at(&expanded_elf, i);
        Match(ExpandedSegment, segment_optional)
            IsSome(segment, PageTable__map(v_ptr(page_table, PageTable), segment.address, segment.size);)
            IsNone(__d_println(PANIC, "Segment not mapped"););
        End
    }
    __d_println(DEBUG, "Code mapped");

    Stack stack = Stack__new();
    Stack__map(&stack, v_ptr(page_table, PageTable));
    __d_println(DEBUG, "Stack mapped");

    Match(ExpandedSegment, ExpandedSegmentArray__at(&expanded_elf, 0))
        IsNone(__d_println(PANIC, "No entry point"););
        IsSome(entry_point,
            {
                Process * proc = Process__new(page_table, entry_point.address, stack);
                Process__start(proc);

                return proc;
            }
        )
    End



    __d_println(DEBUG, "Process created");
}
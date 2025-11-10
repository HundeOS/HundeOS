#include "process.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/process.h"
#include "types/kstring.h"
#include "types/result.h"
#include "types/optional.h"
#include "scheduler/scheduler.h"

#include "PROCESS.h"
#include "kalloc.h"
#include "stack.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(ProcessHandles);

static ProcessID pid_counter = 0;

ProcessPtr Process__new(KString name, KernelAddress page_table, UserAddress entry, Stack stack)
{
    __d_printf(PROCESS, "Creating process with entry point %p\n", entry);
    ProcessPtr proc = (ProcessPtr) Kernel__alloc_kernel(sizeof(Process));
    proc->entry_point = entry;
    proc->page_table = page_table;
    proc->state = PROC_STATE_NEW;
    proc->pid = ++pid_counter;
    proc->stack = stack;
    proc->handles = EmptyArray(ProcessHandles);
    proc->context = ProcessContext__init(proc->pid, &proc->stack, entry, page_table);
    proc->last_tick = 0;
    proc->name = name;
    return proc;
}

Result(ProcessPtr) Process__setup(ElfPtr binary)
{
    __d_println(PROCESS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(binary);

    __d_println(PROCESS, "Creating page table");
    KernelAddress page_table = PageTable__new();

    __d_println(PROCESS, "Page table created");

    KString not_mapped_message = kstring("Segment not mapped");
    for (int i = 0; i < expanded_elf.len; i++)
    {
        Match(ExpandedSegment, ExpandedSegmentArray__at(&expanded_elf, i))
            IsSome(segment,
            {
                UserAddress addr = segment.address;
                __d_printf(MMU, "Mapping segment %i phys=%p, virt=%p\n", i, addr.kernel_addr.phys_addr, addr.virtual_addr);
                PageTable__map(v_ptr(page_table, PageTable), segment.address, segment.size);
            })
            IsNone(return Error(ProcessPtr, not_mapped_message, PROCESS_FAILED_TO_SETUP););
        End
    }
    __d_println(PROCESS, "Code mapped");

    Stack stack = Stack__new();
    Stack__map(&stack, v_ptr(page_table, PageTable));
    __d_println(PROCESS, "Stack mapped");

    Match(ExpandedSegment, ExpandedSegmentArray__at(&expanded_elf, 0))
        IsNone(__d_println(PANIC, "No entry point"););
        IsSome(entry_point,
        {
            KString name = kstring(binary->name);
            ProcessPtr proc = Process__new(name, page_table, entry_point.address, stack);
            return Success(ProcessPtr, proc);
        })
    End
}

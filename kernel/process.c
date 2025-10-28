#include "process.h"
#include "bin/elf.h"
#include "arch/page_table.h"
#include "arch/process.h"
#include "types/kstring.h"
#include "types/result.h"
#include "types/optional.h"

#include "debug.h"
#include "kalloc.h"
#include "stack.h"
#include "mutex.h"

FixedArrayBodyDefine(ProcessTable);
FixedArrayBodyDefine(CPUProcesses);
FixedArrayBodyDefine(CPUProcessTables);

static CPUProcessTables cpu_process_tables = EmptyArray(CPUProcessTables);
static ProcessTable processes = EmptyArray(ProcessTable);
static CPUProcesses cpu_processes = EmptyArray(CPUProcesses);

static mutex_t table_lock = MUTEX_INIT;
static mutex_t cpu_lock = MUTEX_INIT;


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

ProcessTablePtr Process__cpu_table()
{
    ProcessTablePtr result;

    Match(ProcessTablePtr, CPUProcessTables__at(&cpu_process_tables, CPU__id()));
    IsSome(process_table_ptr,
    {
        result = process_table_ptr;
    });
    IsNone(
    {
        result = ProcessTable__heap_new();
        CPUProcessTables__set(&cpu_process_tables, CPU__id(), result);
    });
    End

    return result;
}

ProcessPtr Process__setup_and_start(KernelAddress page_table, ExpandedSegment entry_point, Stack stack)
{
    ProcessPtr proc = Process__new(page_table, entry_point.address, stack);

    mutex_lock(&table_lock);
    ProcessTable__push(&processes, proc);
    mutex_unlock(&table_lock);

    mutex_lock(&cpu_lock);
    __d_printf(FOCUS, "current cpu = %i\n", CPU__id());
    ProcessTablePtr cpu_process_table = Process__cpu_table();
    ProcessTable__push(cpu_process_table, proc);
    mutex_unlock(&cpu_lock);

    ResultMatch(ArrayCode, CPUProcesses__set(&cpu_processes,CPU__id(), proc));
        IsSuccess(_,
        {
            Process__start(proc);
        });
        IsError(code, error, __d_println(PANIC, KString__cstr(&error)));
    End

    Process__start(proc);
    return proc;
}

ProcessPtr Process__spawn_from_elf(Elf * elf) {
    __d_println(FOCUS, "Process__spawn_from_elf");
    ExpandedSegmentArray expanded_elf = Elf__map(elf);

    __d_println(FOCUS, "Creating page table");
    KernelAddress page_table = PageTable__new();

    __d_println(FOCUS, "Page table created");

    for (int i = 0; i < expanded_elf.len; i++)
    {
        Optional(ExpandedSegment) segment_optional = ExpandedSegmentArray__at(&expanded_elf, i);
        Match(ExpandedSegment, segment_optional)
            IsSome(segment, PageTable__map(v_ptr(page_table, PageTable), segment.address, segment.size);)
            IsNone(__d_println(PANIC, "Segment not mapped"););
        End
    }
    __d_println(FOCUS, "Code mapped");

    Stack stack = Stack__new();
    Stack__map(&stack, v_ptr(page_table, PageTable));
    __d_println(FOCUS, "Stack mapped");

    Match(ExpandedSegment, ExpandedSegmentArray__at(&expanded_elf, 0))
        IsNone(__d_println(PANIC, "No entry point"););
        IsSome(entry_point,
        {
            return Process__setup_and_start(page_table, entry_point, stack);
        })
    End

    __d_println(FOCUS, "Process created");
}

Optional(ProcessPtr) Process__current_for_cpu(int cpu)
{
    mutex_lock(&cpu_lock);
    Optional(ProcessPtr) result = CPUProcesses__at(&cpu_processes, cpu);
    mutex_unlock(&cpu_lock);
    return result;
}

Optional(ProcessTablePtr) Process__all_for_cpu(int cpu)
{
    mutex_lock(&cpu_lock);
    Optional(ProcessTablePtr) result = CPUProcessTables__at(&cpu_process_tables, cpu);
    mutex_unlock(&cpu_lock);
    return result;
}

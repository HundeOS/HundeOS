#include "process.h"
#include "arch/page_table.h"

extern Process * Process__new(ProcessEntryPoint entry);
extern uint64_t Process__start(Process * self);
extern PageTable * PageTable__new();
extern void PageTable__grant(Process * self);


ProcessId Process__spawn(ProcessEntryPoint entry) {
    Process * proc = Process__new(entry);
    proc->page_table = PageTable__new();
    PageTable__grant(proc);
    Process__start(proc);

    return proc->pid;
}
#include "process.h"

extern Process * Process__new(ProcessEntryPoint entry);
extern uint64_t Process__start(Process * self);
extern void PageTable__grant(Process * self);


ProcessId Process__spawn(ProcessEntryPoint entry) {
    Process * proc = Process__new(entry);
    PageTable__grant(proc);
    Process__start(proc);

    return proc->pid;
}
#include "context_switch.h"
#include "../process.h"
#include "../arch/process.h"
#include "../debug.h"

ContextSwitch ContextSwitch__new(ProcessPtr new_proc, Optional(ProcessPtr) old_proc)
{
    return (ContextSwitch) {
        .new_proc = new_proc,
        .old_proc = old_proc
    };
}

void ContextSwitch__switch(ContextSwitch self)
{
    Match(ProcessPtr, self.old_proc)
        IsSome(proc,
        {
            if (proc->state != PROC_STATE_DEAD && proc->state != PROC_STATE_WAITING)
            {
                proc->state = PROC_STATE_INTERRUPTED;
            }
        })
    End

    ProcessPtr new_proc = self.new_proc;
    __d_printf(
        PROCESS_SWITCH,
        "ContextSwitch__switch target pid=%i name=%s state=%s context=%p page_table=%p\n",
        new_proc->pid,
        cstr(new_proc->name),
        ProcessState__to_string(new_proc->state),
        new_proc->context,
        paddr(new_proc->page_table)
    );
    __d_printf(PROCESS_SWITCH, "Switch to proc: %i %s - %s.\n",
        new_proc->pid,
        cstr(new_proc->name),
        ProcessState__to_string(new_proc->state));

    new_proc->state = PROC_STATE_RUNNING;
    Process__context_switch(new_proc);
}

#include "context_switch.h"
#include "../process.h"

ContextSwitch ContextSwitch__new(ProcessPtr new_proc, Optional(ProcessPtr) old_proc)
{
    return (ContextSwitch) {
        .new_proc = new_proc,
        .old_proc = old_proc
    };
}

void ContextSwitch__switch(Optional(ContextSwitch) self_opt)
{
    Match(ContextSwitch, self_opt)
        IsSome(self,
        {
            Match(ProcessPtr, self.old_proc)
                IsSome(proc,
                {
                    proc->state = PROC_STATE_INTERRUPTED;
                })
            End

            ProcessPtr new_proc = self.new_proc;
            const char * name = cstr(new_proc->name);
            __d_printf(SCHEDULER, "Switch to proc: %i %s - %s.\n",
                new_proc->pid,
                name,
                ProcessState__to_string(new_proc->state));

            new_proc->state = PROC_STATE_RUNNING;
            Process__context_switch(new_proc);
        })
        IsNone(
        {
            __d_println(SCHEDULER, "No context to switch to.");
        })
    End
}

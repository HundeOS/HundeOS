#ifndef __KERNEL_SCHEDULER_CONTEXT_H
#define __KERNEL_SCHEDULER_CONTEXT_H

#include "../process.h"

typedef struct ContextSwitch
{
    ProcessPtr new_proc;
    Optional(ProcessPtr) old_proc;
} ContextSwitch;

OptionalDef(ContextSwitch);

void ContextSwitch__switch(Optional(ContextSwitch) self_opt);
ContextSwitch ContextSwitch__new(ProcessPtr new_proc, Optional(ProcessPtr) old_proc);


#endif
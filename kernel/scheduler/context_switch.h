#ifndef __KERNEL_SCHEDULER_CONTEXT_H
#define __KERNEL_SCHEDULER_CONTEXT_H

#include "../process.h"

typedef struct ContextSwitch
{
    ProcessPtr new_proc;
    Optional(ProcessPtr) old_proc;
} ContextSwitch;

typedef const ContextSwitch * ConstContextSwitchPtr;

OptionalDef(ContextSwitch);

void ContextSwitch__switch(ConstContextSwitchPtr self);
ContextSwitch ContextSwitch__new(ProcessPtr new_proc, OptionalPtr(ProcessPtr) old_proc);

#endif

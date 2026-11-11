#ifndef __KERNEL_SCHEDULER_CONTEXT_H
#define __KERNEL_SCHEDULER_CONTEXT_H

#include "../process.h"

typedef struct ContextSwitch
{
    ThreadPtr new_thread;
    Optional(ThreadPtr) old_thread;
} ContextSwitch;

typedef const ContextSwitch * ConstContextSwitchPtr;

OptionalDef(ContextSwitch);

void ContextSwitch__switch(ConstContextSwitchPtr self);
ContextSwitch ContextSwitch__new(ThreadPtr new_thread, OptionalPtr(ThreadPtr) old_thread);

#endif

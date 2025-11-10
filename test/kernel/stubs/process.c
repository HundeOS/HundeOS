#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/fixed_array.h"


FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessArray, &Ptr__equals);
FixedArrayBodyDefine(ProcessArray);

static ProcessPtr current_process = NULL;

void Process__context_switch(ProcessPtr self)
{
    current_process = self;
}

void current_process_reset()
{
    current_process = NULL;
}

ProcessPtr current_running_process()
{
    return current_process;
}

void Process__save_context(ProcessPtr self, ProcessContextPtr context)
{
    self->context = context;
}

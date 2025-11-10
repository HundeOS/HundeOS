#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/fixed_array.h"


FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessArray, &Ptr__equals);
FixedArrayBodyDefine(ProcessArray);

static ProcessArray started_processes = EmptyArray(ProcessArray);

void Process__start(Process * self)
{
    ProcessArray__push(&started_processes, self);
}

void reset_started_processes(void)
{
    started_processes.len = 0;
}

void Process__context_switch(ProcessPtr self)
{

}

void Process__save_context(ProcessPtr self, ProcessContextPtr context)
{
    self->context = context;
}

bool process_is_started(Process * proc)
{
    for (int i = 0; i < started_processes.len; i++)
    {
        Match(ProcessPtr, ProcessArray__at(&started_processes, i))
            IsSome(process,
                if (process == proc)
                {
                    return true;
                }
            )
            IsNone();
        End
    }

    return false;
}

#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/fixed_array.h"

typedef Process * ProcessPtr;

FixedArrayDefine(ProcessPtr, 1024, ProcessArray);
FixedArrayBodyDefine(ProcessArray);

static ProcessArray started_processes = EmptyArray(ProcessArray);

extern uint64_t Process__start(Process * self)
{
    ProcessArray__push(&started_processes, self);
    return 100;
}

void reset_started_processes(void)
{
    started_processes.len = 0;
}

bool process_is_started(Process * proc)
{
    for (int i = 0; i < started_processes.len; i++)
    {
        if (ProcessArray__at(&started_processes, i) == proc)
        {
            return true;
        }
    }

    return false;
}

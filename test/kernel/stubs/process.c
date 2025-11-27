#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"

#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/types/fixed_array.h"


FixedArrayDefine(ProcessPtr, MAX_PROCESSES, ProcessArray, &Ptr__equals);
FixedArrayBodyDefine(ProcessArray);

static ProcessPtr current_process = NULL;

static Result(Anything) deferred_syscall_res;

Result(Anything) deferred_syscall_result()
{
    return deferred_syscall_res;
}

void clear_deferred_syscall_result()
{
    deferred_syscall_res = Error(Anything, kstring("no syscall result"), -1);
}

void Process__context_switch(ProcessPtr self)
{
    current_process = self;
    Match(ParkedKernelContextPtr, self->parked_context)
        IsSome(parked_kernel_context,
        {
            Match(Continuation, parked_kernel_context->continuation)
                IsSome(continuation,
                {
                    Result(Anything) syscall_res = continuation(parked_kernel_context);
                    deferred_syscall_res = syscall_res;
                })
            End
        })
    End
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

#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"
#include "process_context_layout.h"

static ProcessPtr current_process = NULL;
static ProcessPtr active_process = NULL;

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
}

void Process__activate_address_space(ProcessPtr self)
{
    active_process = self;
}

void current_process_reset()
{
    current_process = NULL;
    active_process = NULL;
}

ProcessPtr current_running_process()
{
    return current_process;
}

ProcessPtr active_address_space_process()
{
    return active_process;
}

void Process__save_context(ProcessPtr self, ProcessContextPtr context)
{
    self->context = context;
}

void Process__set_syscall_result(ProcessPtr self, SystemCallResult result)
{
    if (self->context != NULL)
    {
        self->context->x[0] = result;
    }
    deferred_syscall_res = Success(Anything, result);
}

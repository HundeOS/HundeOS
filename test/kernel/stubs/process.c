#include <stdint.h>
#include "../../../kernel/arch/process.h"
#include "../../../kernel/process.h"
#include "process_context_layout.h"
#include "process.h"
#include "../../../kernel/kernel_core.h"
static TestStartupStdio initial_stdio;
static bool saved_with_scheduler_lock;
static bool switched_with_scheduler_lock;

static bool scheduler_lock_held(ProcessPtr process)
{
    MutexPtr lock = &process->kernel->scheduler->scheduler_lock;
    if (mutex_trylock(lock))
    {
        mutex_unlock(lock);
        return false;
    }
    return true;
}

bool context_saved_with_scheduler_lock(void)
{
    return saved_with_scheduler_lock;
}

bool context_switched_with_scheduler_lock(void)
{
    return switched_with_scheduler_lock;
}
uint16_t Arch__elf_machine(void)
{
    return 0xB7;
}
TestStartupStdio startup_stdio(void)
{
    return initial_stdio;
}

static ProcessPtr current_process = NULL;
static ProcessPtr active_process = NULL;

static Result(Anything) deferred_syscall_res;

Result(Anything) deferred_syscall_result()
{
    return deferred_syscall_res;
}

void clear_deferred_syscall_result()
{
    deferred_syscall_res = Error(Anything, bstring("no syscall result"), -1);
}

void Process__context_switch(ProcessPtr self)
{
    switched_with_scheduler_lock = scheduler_lock_held(self);
    current_process = self;
}

void Process__activate_address_space(ProcessPtr self)
{
    active_process = self;
}

void Process__set_startup_stdio(ProcessPtr self, int64_t input, int64_t output, int64_t error)
{
    initial_stdio = (TestStartupStdio){.pid = self->pid, .input = input, .output = output, .error = error};
}

void current_process_reset()
{
    saved_with_scheduler_lock = false;
    switched_with_scheduler_lock = false;
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
    saved_with_scheduler_lock = scheduler_lock_held(self);
    if (context == NULL) // hundelint: ignore-no-null
    {
        return;
    }

    *self->context = *context;
}

void Process__set_syscall_result(ProcessPtr self, SystemCallResult result)
{
    if (self->context != NULL)
    {
        self->context->x[0] = result;
    }
    deferred_syscall_res = Success(Anything, result);
}

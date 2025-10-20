#include "kernel_init.h"
#include "mutex.h"
#include "panic.h"
#include "spin.h"
#include "subsystems/io/register.h"
#include "syscall/init.h"
#include "syscall/syscall_dispatcher.h"
#include "subsystems/io/register.h"

static Kernel instance = {
    .status = INIT_NOT_STARTED,
    .init_lock = MUTEX_INIT,
    .syscall_dispatcher = NEW_SYSCALL_DISPATCHER,
    .io_subsystem = NEW_IO_SUBSYSTEM,
    .heap = NEW_HEAP,
};


KernelPtr Kernel__instance()
{
    return &instance;
}

void __kernel_panic(KernelPtr self, KString message)
{
    self->status = INIT_FAILED_AND_PANICKED;
    mutex_unlock(&self->init_lock);
    Kernel__panic(KString__cstr(&message), NULL);
}


void Kernel__io_subsystem_init(KernelPtr self)
{
    __d_println(DEBUG, "Kernel__io_subsystem_init");
    ResultMatch(RegisterCode, IOSubsystem__register_devices(&self->io_subsystem));
        IsError(code, message,
        {
            __kernel_panic(self, message);
        });
    End
}

void Kernel__init_syscall_handlers(KernelPtr self)
{
    ResultMatch(RegisterResult, Init__syscalls(&self->syscall_dispatcher);)
        IsError(code, message,
        {
            __kernel_panic(self, message);
        });
    End
}

void Kernel__init_heap(KernelPtr self)
{
    ResultMatch(HeapCode, Heap__init(&self->heap);)
        IsError(code, message,
        {
            __kernel_panic(self, message);
        });
    End
}

void Kernel__init(KernelPtr self)
{
    __d_println(DEBUG, "Kernel__init");
    mutex_lock(&self->init_lock);
    self->status = INIT_IN_PROGRESS;

    Kernel__init_heap(self);
    Kernel__init_syscall_handlers(self);
    Kernel__io_subsystem_init(self);

    self->status = INIT_DONE;
    mutex_unlock(&self->init_lock);
}

InitStatus Kernel__init_status(KernelPtr self)
{
    mutex_lock(&self->init_lock);
    InitStatus status = self->status;
    mutex_unlock(&self->init_lock);

    return status;
}

const char * FAILED_TO_INIT_MSG = "Kernel failed to initialize";;

volatile bool Kernel__ready(void * data)
{
    KernelPtr self = data;
    return Kernel__init_status(self) != INIT_DONE && Kernel__init_status(self) != INIT_FAILED_AND_PANICKED;
}

Result(InitStatus) Kernel__wait_for_init(KernelPtr self)
{
    Spin__wait_context(Kernel__ready, self, MAX_SPINS);
    InitStatus status = self->status;

    if (status == INIT_DONE)
    {
        return Success(InitStatus, INIT_DONE);
    }
    if (status == INIT_FAILED_AND_PANICKED)
    {
        Kernel__panic(FAILED_TO_INIT_MSG, NULL);
        return Error(InitStatus, KString__new(FAILED_TO_INIT_MSG), status);
    }
}
#include "arch/init.h"
#include "kernel_init.h"
#include "mutex.h"
#include "panic.h"
#include "spin.h"
#include "types/bstring.h"
#include "subsystems/io/register.h"
#include "syscall/init.h"
#include "syscall/syscall_dispatcher.h"
#include "kernel_core.h"
#include "irq/irq_dispatcher.h"
#include "bin/embedded_binaries.h"
#include "scheduler/register.h"
#include "scheduler/scheduler.h"


static BString failed_to_find_idle_process = bstr("failed to find idle process");
static BString heap_init_progress = bstr("heap initialised");
static BString syscall_init_progress = bstr("syscall handlers initialised");
static BString io_init_progress = bstr("io subsystem initialised");
static BString scheduler_init_progress = bstr("scheduler initialised");
static BString arch_init_progress = bstr("architecture specific components initialised");
static BString failed_to_init_msg = bstr("failed to initialise kernel");
static BString missing_system_info = bstr("missing system info");

static MessageBroker message_broker = NEW_BROKER;
static Scheduler scheduler = NEW_SCHEDULER(&message_broker);
static IRQDispatcher irq_dispatcher = NEW_IRQ_DISPATCHER(&scheduler);
static IOSubsystem io_subsystem = NEW_IO_SUBSYSTEM(&scheduler, &message_broker);

static Kernel instance = {
    .status = INIT_NOT_STARTED,
    .init_lock = MUTEX_INIT,
    .syscall_dispatcher = NEW_SYSCALL_DISPATCHER,
    .io_subsystem = &io_subsystem,
    .system_info = None(SystemInfo),
    .heap = NEW_HEAP,
    .scheduler = &scheduler,
    .message_broker = &message_broker,
    .irq_dispatcher = &irq_dispatcher,
    .auth = NEW_AUTH,
};

void __kernel_panic(KernelPtr self, BStringPtr message)
{
    __atomic_store_n(&self->status, INIT_FAILED_AND_PANICKED, __ATOMIC_RELEASE);
    mutex_unlock(&self->init_lock);
    Kernel__panic(cstr_p(message), NULL); // hundelint: ignore-no-null
}

void PanicOnError(Result(SubsystemInitStatus) res, KernelPtr kernel, BStringPtr progress_message)
{
    match_result(SubsystemInitStatus, res,
        success(_)
        {
            __d_println(PROGRESS, BString__cstr(progress_message));
        },
        error(code, message)
        {
            __kernel_panic(kernel, &message);
        }
    );
}

KernelPtr Kernel__instance()
{
    return &instance;
}

Result(SubsystemInitStatus) Kernel__io_subsystem_init(KernelPtr self)
{
    __d_println(DEBUG, "Kernel__io_subsystem_init");
    match_result(RegisterCode, IOSubsystem__register(self),
        success_do_nothing(),
        error(code, message)
        {
            return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
        }
    );
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

Result(SubsystemInitStatus) Kernel__init_syscall_handlers(KernelPtr self)
{
    match_result(RegisterResult, Init__syscalls(&self->syscall_dispatcher);,
        success_do_nothing(),
        error(code, message)
        {
            return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
        }
    );
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

Result(SubsystemInitStatus) Kernel__init_heap(KernelPtr self)
{
    if (is_nothing(self->system_info))
    {
        return Error(SubsystemInitStatus, missing_system_info, SUBSYSTEM_INIT_FAILED);
    }

    SystemInfoPtr system_info = &self->system_info.some.val;
    match_result(HeapCode, Heap__init(&self->heap, system_info);,
        success_do_nothing(),
        error(code, message)
        {
            return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
        }
    );

    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

Result(SubsystemInitStatus) Kernel__init_scheduler(KernelPtr self)
{
    if (is_nothing(self->system_info))
    {
        return Error(SubsystemInitStatus, missing_system_info, SUBSYSTEM_INIT_FAILED);
    }

    SystemInfoPtr system_info = &self->system_info.some.val;
    self->scheduler->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);
    self->irq_dispatcher->interrupt_controller = Some(InterruptControllerInfo, system_info->interrupt_controller);

    Optional(Elf) idle_opt = EmbeddedElf__load(bstring("idle"));
    match_optional(Elf, idle_opt,
        some(idle)
        {
            match_result(SchedulerInitStatus, Scheduler__init(self, self->scheduler, idle);,
                success_do_nothing(),
                error(code, message)
                {
                    return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED);
                }
            );
        },
        none()
        {
            return Error(SubsystemInitStatus, failed_to_find_idle_process, SUBSYSTEM_INIT_FAILED);
        }
    );

    match_result(SchedulerRegisterCode, Scheduler__register(self),
        success_do_nothing(),
        error(code, message) { return Error(SubsystemInitStatus, message, SUBSYSTEM_INIT_FAILED); }
    );
    return Success(SubsystemInitStatus, SUBSYSTEM_INIT_OK);
}

void Kernel__init(KernelPtr self)
{
    __d_println(DEBUG, "Kernel__init");
    mutex_lock(&self->init_lock);
    __atomic_store_n(&self->status, INIT_IN_PROGRESS, __ATOMIC_RELEASE);

    PanicOnError(Kernel__init_heap(self), self, &heap_init_progress);
    PanicOnError(Kernel__init_syscall_handlers(self), self, &syscall_init_progress);
    PanicOnError(Kernel__io_subsystem_init(self), self, &io_init_progress);
    PanicOnError(Kernel__init_scheduler(self), self, &scheduler_init_progress);
    PanicOnError(Arch__init(self), self, &arch_init_progress);

    __atomic_store_n(&self->status, INIT_DONE, __ATOMIC_RELEASE);
    mutex_unlock(&self->init_lock);
}

InitStatus Kernel__init_status(KernelPtr self)
{
    mutex_lock(&self->init_lock);
    InitStatus status = __atomic_load_n(&self->status, __ATOMIC_ACQUIRE);
    mutex_unlock(&self->init_lock);

    return status;
}

volatile bool Kernel__ready(void * data)
{
    KernelPtr self = data;
    InitStatus status = __atomic_load_n(&self->status, __ATOMIC_ACQUIRE);
    return status != INIT_DONE && status != INIT_FAILED_AND_PANICKED;
}

Result(InitStatus) Kernel__wait_for_init(KernelPtr self)
{
    Spin__wait_context(Kernel__ready, self, MAX_SPINS);
    InitStatus status = __atomic_load_n(&self->status, __ATOMIC_ACQUIRE);

    if (status == INIT_DONE)
    {
        return Success(InitStatus, INIT_DONE);
    }
    if (status == INIT_FAILED_AND_PANICKED)
    {
        Kernel__panic(cstr(failed_to_init_msg), NULL); // hundelint: ignore-no-null
        return Error(InitStatus, failed_to_init_msg, status);
    }

    return Error(InitStatus, failed_to_init_msg, status);
}

#include "queue_wait.h"
#include "ipc/ipc_platform.h"
#include "ipc/queue_wait_control.h"
#include "boot_info.h"
#include "syscall.h"
#include "syscalls/service.h"

static bool prepared;
static IpcEventGeneration prepared_doorbell_generation;
static Optional(QueueWaitControlPtr) wait_control = None(QueueWaitControlPtr);
static bool registration_attempted;

void QueueWait__reset_after_fork(void)
{
    QueueWait__swap_adapter(&None(QueueWaitAdapter));
    prepared = false;
    prepared_doorbell_generation = 0;
    wait_control = None(QueueWaitControlPtr);
    registration_attempted = false;
    IODiagnostics__detach();
}

static void QueueWait__register_control(void)
{
    if (registration_attempted)
    {
        return;
    }
    registration_attempted = true;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            IpcPlatform__configure_wait(info.wait_mode);
            IpcPlatform__configure_timer_frequency(info.timer_frequency);
        },
        error(code, message)
        {
            IpcPlatform__configure_wait(BOOT_WAIT_SPIN);
            IpcPlatform__configure_timer_frequency(0);
        }
    );
    SystemCallResult address = syscall(SERVICE_CALL, SERVICE_QUEUE_WAIT_CONTROL, 0, 0, 0, 0);
    if ((int64_t)address > 0)
    {
        wait_control = Some(QueueWaitControlPtr, (QueueWaitControlPtr)address);
        IODiagnostics__attach(&((QueueWaitControlPtr)address)->diagnostics);
    }
}

Optional(QueueDispatchTrace) QueueWait__take_dispatch_trace(ConstRequestTicketPtr ticket)
{
    match_optional(
        wait_control,
        some(control)
        {
            return QueueWaitControl__take_dispatch(control, ticket);
        },
        none_do_nothing()
    );
    return None(QueueDispatchTrace);
}

static void QueueWait__clear_control(void)
{
    match_optional(wait_control,
        some(control)
        {
            QueueWaitControl__clear(control);
        },
        none_do_nothing()
    );
}

static void QueueWait__complete_control(void)
{
    match_optional(
        wait_control,
        some(control)
        {
            QueueWaitControl__complete_wait(control, IpcPlatform__monotonic_usec());
        },
        none_do_nothing()
    );
}

static void QueueWait__arm_control(IpcEventGeneration observed)
{
    QueueWait__register_control();
    match_optional(
        wait_control,
        some(control)
        {
            QueueWaitControl__begin(control);
            QueueWaitControl__commit(control, IpcPlatform__monotonic_usec(), QUEUE_WAIT_NO_DEADLINE, observed);
        },
        none_do_nothing()
    );
}

static bool QueueWait__changed(IpcEventGeneration observed)
{
    return IpcEvent__changed(IpcDoorbell__event(IpcPlatform__local_doorbell()), observed);
}

static bool QueueWait__wait(IpcEventGeneration observed)
{
    if (QueueWait__has_adapter())
    {
        if (QueueWait__changed(observed))
        {
            return true;
        }
        match_result(QueueWait__cooperate(),
            success(code)
            {
                return true;
            },
            error(code, message)
            {
                return false;
            }
        );
        return false;
    }
    QueueWait__arm_control(observed);
    IODiagnostics__record(IO_DIAGNOSTIC_WAIT_ENTRIES, 1);
    SystemCallResult result = syscall(SERVICE_CALL, SERVICE_QUEUE_WAIT, observed, QUEUE_WAIT_INDEFINITE_TICKS, 0, 0);
    QueueWait__complete_control();
    return (int64_t)result != SYSCALL_INTERRUPTED;
}

void Queue__prepare_wait(void)
{
    QueueWait__register_control();
    QueueWait__clear_control();
    prepared_doorbell_generation = IpcPlatform__prepare_wait(IpcDoorbell__event(IpcPlatform__local_doorbell()));
    prepared = true;
}

void Queue__wait_event(void)
{
    if (prepared && !QueueWait__changed(prepared_doorbell_generation))
    {
        QueueWait__wait(prepared_doorbell_generation);
    }
    Queue__prepare_wait();
}

bool Queue__wait_readable_interruptible(QueuePtr self)
{
    while (true)
    {
        IpcEventGeneration observed = IpcPlatform__prepare_wait(IpcDoorbell__event(IpcPlatform__local_doorbell()));
        QueueCode status = Queue__status(self);
        if (status == QUEUE_CLOSED || Queue__has_unread_messages(self))
        {
            return true;
        }
        if (!QueueWait__wait(observed))
        {
            return false;
        }
    }
}

bool Queue__wait_writable_interruptible(QueuePtr self)
{
    while (true)
    {
        IpcEventGeneration observed = IpcPlatform__prepare_wait(IpcDoorbell__event(IpcPlatform__local_doorbell()));
        if (Queue__has_write_capacity(self))
        {
            return true;
        }
        if (!QueueWait__wait(observed))
        {
            return false;
        }
    }
}

void Queue__wait_readable(QueuePtr self)
{
    Queue__wait_readable_interruptible(self);
}

void Queue__wait_writable(QueuePtr self)
{
    Queue__wait_writable_interruptible(self);
}

void QueueArrivals__wait(QueueArrivalsPtr self, QueueEpoch observed_epoch)
{
    while (QueueArrivals__epoch(self) == observed_epoch)
    {
        IpcEventGeneration observed = IpcPlatform__prepare_wait(IpcDoorbell__event(IpcPlatform__local_doorbell()));
        if (QueueArrivals__epoch(self) != observed_epoch || !QueueWait__wait(observed))
        {
            return;
        }
    }
}

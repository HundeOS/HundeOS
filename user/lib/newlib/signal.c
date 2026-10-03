#include <errno.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "include/signal.h"
#include "../sys/process.h"

static bool Signal__valid(int signal)
{
    return signal > 0 && signal < NSIG;
}

static ProcessSignalMask Signal__mask(int signal)
{
    return (ProcessSignalMask)1U << (signal - 1);
}

static int Signal__control_error(ProcessSignalResult result)
{
    if (result == PROCESS_SIGNAL_PROCESS_NOT_FOUND)
    {
        errno = ESRCH;
    }
    else if (result == PROCESS_SIGNAL_UNSUPPORTED)
    {
        errno = EOPNOTSUPP;
    }
    else if (result == PROCESS_SIGNAL_PERMISSION_DENIED)
    {
        errno = EPERM;
    }
    else
    {
        errno = EINVAL;
    }
    return -1;
}

int kill(pid_t pid, int signal)
{
    if (signal < 0 || signal >= NSIG)
    {
        errno = EINVAL;
        return -1;
    }

    ProcessSignalResult result = kill_process((int64_t)pid, (ProcessSignal)signal);
    if (result != PROCESS_SIGNAL_OK)
    {
        return Signal__control_error(result);
    }
    return 0;
}

int raise(int signal)
{
    return kill((pid_t)process_current_pid(), signal);
}

int killpg(pid_t process_group_id, int signal)
{
    if (process_group_id <= 0)
    {
        errno = EINVAL;
        return -1;
    }
    return kill(-process_group_id, signal);
}

int sigemptyset(sigset_t * set)
{
    if (set == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    *set = 0;
    return 0;
}

int sigfillset(sigset_t * set)
{
    if (set == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    *set = UINT32_MAX;
    return 0;
}

int sigaddset(sigset_t * set, int signal)
{
    if (set == NULL || !Signal__valid(signal))
    {
        errno = EINVAL;
        return -1;
    }
    *set |= Signal__mask(signal);
    return 0;
}

int sigdelset(sigset_t * set, int signal)
{
    if (set == NULL || !Signal__valid(signal))
    {
        errno = EINVAL;
        return -1;
    }
    *set &= ~Signal__mask(signal);
    return 0;
}

int sigismember(const sigset_t * set, int signal)
{
    if (set == NULL || !Signal__valid(signal))
    {
        errno = EINVAL;
        return -1;
    }
    return (*set & Signal__mask(signal)) != 0;
}

int sigaction(int signal, const struct sigaction * action, struct sigaction * previous)
{
    if (!Signal__valid(signal) || (action != NULL && (signal == SIGKILL || signal == SIGSTOP)))
    {
        errno = EINVAL;
        return -1;
    }

    ProcessSignalControlRequest request = {
        .operation = PROCESS_SIGNAL_CONTROL_ACTION,
        .signal = (ProcessSignal)signal,
        .replace = action != NULL,
    };
    if (action != NULL)
    {
        uint64_t handler = (uint64_t)(uintptr_t)action->sa_handler;
        uint64_t restorer =
            handler > PROCESS_SIGNAL_IGNORE_HANDLER ? (uint64_t)(uintptr_t)process_signal_restorer : 0;
        request.action = ProcessSignalAction__new(handler, restorer, action->sa_mask, (uint64_t)action->sa_flags);
    }

    ProcessSignalResult result = process_signal_control(&request);
    if (result != PROCESS_SIGNAL_OK)
    {
        return Signal__control_error(result);
    }
    if (previous != NULL)
    {
        previous->sa_handler = (sig_t)(uintptr_t)request.previous_action.handler;
        previous->sa_mask = (sigset_t)request.previous_action.mask;
        previous->sa_flags = (int)request.previous_action.flags;
    }
    return 0;
}

sig_t signal(int signal, sig_t handler)
{
    struct sigaction action = {
        .sa_handler = handler,
        .sa_mask = 0,
        .sa_flags = SA_RESTART,
    };
    struct sigaction previous;
    if (sigaction(signal, &action, &previous) != 0)
    {
        return SIG_ERR;
    }
    return previous.sa_handler;
}

int sigprocmask(int operation, const sigset_t * set, sigset_t * previous)
{
    if (set != NULL && operation != SIG_BLOCK && operation != SIG_UNBLOCK && operation != SIG_SETMASK)
    {
        errno = EINVAL;
        return -1;
    }

    ProcessSignalControlRequest request = {
        .operation = PROCESS_SIGNAL_CONTROL_MASK,
        .replace = set != NULL,
        .mask_operation = (uint64_t)operation,
        .mask = set == NULL ? 0 : *set,
    };
    ProcessSignalResult result = process_signal_control(&request);
    if (result != PROCESS_SIGNAL_OK)
    {
        return Signal__control_error(result);
    }
    if (previous != NULL)
    {
        *previous = (sigset_t)request.previous_mask;
    }
    return 0;
}

int sigpending(sigset_t * set)
{
    if (set == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    ProcessSignalControlRequest request = {
        .operation = PROCESS_SIGNAL_CONTROL_PENDING,
    };
    ProcessSignalResult result = process_signal_control(&request);
    if (result != PROCESS_SIGNAL_OK)
    {
        return Signal__control_error(result);
    }
    *set = (sigset_t)request.previous_mask;
    return 0;
}

int sigsuspend(const sigset_t * mask)
{
    if (mask == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    ProcessSignalControlRequest request = {
        .operation = PROCESS_SIGNAL_CONTROL_SUSPEND,
        .mask = *mask,
    };
    ProcessSignalResult result = process_signal_control(&request);
    if (result != PROCESS_SIGNAL_INTERRUPTED)
    {
        return Signal__control_error(result);
    }
    errno = EINTR;
    return -1;
}

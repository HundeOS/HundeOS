#include <errno.h>
#include <setjmp.h>

#include "unity.h"

#include "../../../common_includes/syscalls/process.h"
#include "../../../user/lib/newlib/include/signal.h"

void _exit(int code) __attribute__((noreturn));

static uint64_t current_pid;
static unsigned kill_requests;
static int64_t requested_pid;
static ProcessSignal requested_signal;
static ProcessSignal first_requested_signal;
static ProcessSignalResult signal_result;
static ProcessSignalResult control_result;
static ProcessSignalAction installed_actions[NSIG];
static ProcessSignalMask installed_mask;
static ProcessSignalMask pending_mask;
static jmp_buf abort_return;
static int abort_fallback_code;

static ProcessSignalMask test_signal_mask(int signal)
{
    return (ProcessSignalMask)1U << (signal - 1);
}

ProcessSignalResult kill_process(int64_t pid, ProcessSignal signal)
{
    kill_requests++;
    requested_pid = pid;
    requested_signal = signal;
    if (kill_requests == 1)
    {
        first_requested_signal = signal;
    }
    return signal_result;
}

ProcessSignalResult process_signal_control(ProcessSignalControlRequestPtr request)
{
    if (control_result != PROCESS_SIGNAL_OK)
    {
        return control_result;
    }
    if (request->operation == PROCESS_SIGNAL_CONTROL_ACTION)
    {
        if (request->signal == 0 || request->signal >= NSIG ||
            (request->replace != 0 && (request->signal == SIGKILL || request->signal == SIGSTOP)))
        {
            return PROCESS_SIGNAL_INVALID_ARGUMENT;
        }
        request->previous_action = installed_actions[request->signal];
        if (request->replace != 0)
        {
            request->action.mask &= ~(test_signal_mask(SIGKILL) | test_signal_mask(SIGSTOP));
            installed_actions[request->signal] = request->action;
        }
        return PROCESS_SIGNAL_OK;
    }
    if (request->operation == PROCESS_SIGNAL_CONTROL_MASK)
    {
        request->previous_mask = installed_mask;
        if (request->replace == 0)
        {
            return PROCESS_SIGNAL_OK;
        }
        ProcessSignalMask requested = request->mask & ~(test_signal_mask(SIGKILL) | test_signal_mask(SIGSTOP));
        if (request->mask_operation == PROCESS_SIGNAL_MASK_BLOCK)
        {
            installed_mask |= requested;
            return PROCESS_SIGNAL_OK;
        }
        if (request->mask_operation == PROCESS_SIGNAL_MASK_UNBLOCK)
        {
            installed_mask &= ~requested;
            return PROCESS_SIGNAL_OK;
        }
        if (request->mask_operation == PROCESS_SIGNAL_MASK_SET)
        {
            installed_mask = requested;
            return PROCESS_SIGNAL_OK;
        }
        return PROCESS_SIGNAL_INVALID_ARGUMENT;
    }
    if (request->operation == PROCESS_SIGNAL_CONTROL_PENDING)
    {
        request->previous_mask = pending_mask;
        return PROCESS_SIGNAL_OK;
    }
    if (request->operation == PROCESS_SIGNAL_CONTROL_SUSPEND)
    {
        return PROCESS_SIGNAL_INTERRUPTED;
    }
    return PROCESS_SIGNAL_UNSUPPORTED;
}

void process_signal_restorer(void)
{
    __builtin_trap();
}

uint64_t process_current_pid(void)
{
    return current_pid;
}

void _exit(int code)
{
    abort_fallback_code = code;
    longjmp(abort_return, 1);
}

#include "../../../user/lib/newlib/signal.c"
#include "../../../user/lib/newlib/abort.c"

void setUp(void)
{
    for (int signal = 0; signal < NSIG; signal++)
    {
        installed_actions[signal] = (ProcessSignalAction){};
    }
    installed_mask = 0;
    pending_mask = 0;
    errno = 0;
    current_pid = 17;
    kill_requests = 0;
    requested_pid = 0;
    requested_signal = 0;
    first_requested_signal = 0;
    signal_result = PROCESS_SIGNAL_OK;
    control_result = PROCESS_SIGNAL_OK;
    abort_fallback_code = 0;
}

void tearDown(void)
{
}

void test_signal_numbers_match_kernel_supported_signals(void)
{
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INTERRUPT, SIGINT);
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_ABORT, SIGABRT);
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_KILL, SIGKILL);
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_TERMINATE, SIGTERM);
}

void test_signal_set_operations_cover_full_openbsd_range(void)
{
    sigset_t set;

    TEST_ASSERT_EQUAL(0, sigemptyset(&set));
    TEST_ASSERT_EQUAL(0, sigismember(&set, SIGINT));
    TEST_ASSERT_EQUAL(0, sigaddset(&set, SIGINT));
    TEST_ASSERT_EQUAL(1, sigismember(&set, SIGINT));
    TEST_ASSERT_EQUAL(0, sigaddset(&set, SIGTHR));
    TEST_ASSERT_EQUAL(1, sigismember(&set, SIGTHR));
    TEST_ASSERT_EQUAL(0, sigdelset(&set, SIGINT));
    TEST_ASSERT_EQUAL(0, sigismember(&set, SIGINT));
    TEST_ASSERT_EQUAL(0, sigfillset(&set));
    TEST_ASSERT_EQUAL(1, sigismember(&set, SIGUSR2));
}

void test_signal_set_operations_reject_invalid_signals(void)
{
    sigset_t set = 0;

    TEST_ASSERT_EQUAL(-1, sigaddset(&set, 0));
    TEST_ASSERT_EQUAL(EINVAL, errno);
    TEST_ASSERT_EQUAL(-1, sigdelset(&set, NSIG));
    TEST_ASSERT_EQUAL(EINVAL, errno);
    TEST_ASSERT_EQUAL(-1, sigismember(&set, -1));
    TEST_ASSERT_EQUAL(EINVAL, errno);
}

void test_kill_passes_positive_process_and_signal_to_kernel(void)
{
    TEST_ASSERT_EQUAL(0, kill(42, SIGTERM));
    TEST_ASSERT_EQUAL(1, kill_requests);
    TEST_ASSERT_EQUAL_UINT64(42, requested_pid);
    TEST_ASSERT_EQUAL_UINT64(SIGTERM, requested_signal);
}

void test_kill_zero_signal_probes_process(void)
{
    TEST_ASSERT_EQUAL(0, kill(42, 0));
    TEST_ASSERT_EQUAL(1, kill_requests);
    TEST_ASSERT_EQUAL_UINT64(0, requested_signal);
}

void test_kill_maps_missing_process_to_esrch(void)
{
    signal_result = PROCESS_SIGNAL_PROCESS_NOT_FOUND;

    TEST_ASSERT_EQUAL(-1, kill(42, SIGTERM));
    TEST_ASSERT_EQUAL(ESRCH, errno);
}

void test_kill_passes_process_group_targets_to_kernel(void)
{
    TEST_ASSERT_EQUAL(0, kill(0, SIGTERM));
    TEST_ASSERT_EQUAL_INT64(0, requested_pid);
    TEST_ASSERT_EQUAL(0, kill(-42, SIGTERM));
    TEST_ASSERT_EQUAL_INT64(-42, requested_pid);
    TEST_ASSERT_EQUAL(2, kill_requests);
}

void test_raise_targets_current_process(void)
{
    TEST_ASSERT_EQUAL(0, raise(SIGINT));
    TEST_ASSERT_EQUAL(1, kill_requests);
    TEST_ASSERT_EQUAL_UINT64(current_pid, requested_pid);
    TEST_ASSERT_EQUAL_UINT64(SIGINT, requested_signal);
}

void test_sigaction_stores_and_returns_process_local_dispositions(void)
{
    struct sigaction action = {
        .sa_handler = SIG_IGN,
        .sa_mask = Signal__mask(SIGTERM) | Signal__mask(SIGKILL),
        .sa_flags = SA_RESTART,
    };
    struct sigaction previous;

    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGINT, &action, &previous));
    TEST_ASSERT_EQUAL_PTR(SIG_DFL, previous.sa_handler);

    struct sigaction installed;
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGINT, NULL, &installed));
    TEST_ASSERT_EQUAL_PTR(SIG_IGN, installed.sa_handler);
    TEST_ASSERT_EQUAL_INT(1, sigismember(&installed.sa_mask, SIGTERM));
    TEST_ASSERT_EQUAL_INT(0, sigismember(&installed.sa_mask, SIGKILL));
    TEST_ASSERT_EQUAL_INT(SA_RESTART, installed.sa_flags);
}

void test_signal_returns_the_previous_handler(void)
{
    TEST_ASSERT_EQUAL_PTR(SIG_DFL, signal(SIGTERM, SIG_IGN));
    TEST_ASSERT_EQUAL_PTR(SIG_IGN, signal(SIGTERM, SIG_DFL));
}

void test_sigaction_rejects_invalid_and_unmodifiable_signals(void)
{
    struct sigaction action = {.sa_handler = SIG_IGN};

    TEST_ASSERT_EQUAL_INT(-1, sigaction(0, &action, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(-1, sigaction(SIGKILL, &action, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(-1, sigaction(SIGSTOP, &action, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
    TEST_ASSERT_EQUAL_INT(0, sigaction(SIGKILL, NULL, NULL));
}

void test_sigprocmask_blocks_unblocks_and_replaces_the_local_mask(void)
{
    sigset_t requested = Signal__mask(SIGINT) | Signal__mask(SIGTERM) | Signal__mask(SIGKILL);
    sigset_t previous;

    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_BLOCK, &requested, &previous));
    TEST_ASSERT_EQUAL_UINT32(0, previous);
    sigset_t current;
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, NULL, &current));
    TEST_ASSERT_EQUAL_INT(1, sigismember(&current, SIGINT));
    TEST_ASSERT_EQUAL_INT(1, sigismember(&current, SIGTERM));
    TEST_ASSERT_EQUAL_INT(0, sigismember(&current, SIGKILL));

    sigset_t interrupt = Signal__mask(SIGINT);
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_UNBLOCK, &interrupt, &previous));
    TEST_ASSERT_EQUAL_INT(1, sigismember(&previous, SIGINT));
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, NULL, &current));
    TEST_ASSERT_EQUAL_INT(0, sigismember(&current, SIGINT));

    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, &interrupt, NULL));
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, NULL, &current));
    TEST_ASSERT_EQUAL_UINT32(interrupt, current);
}

void test_sigprocmask_query_ignores_the_operation_and_rejects_invalid_updates(void)
{
    sigset_t requested = Signal__mask(SIGINT);
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_BLOCK, &requested, NULL));

    sigset_t current;
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(99, NULL, &current));
    TEST_ASSERT_EQUAL_UINT32(requested, current);
    TEST_ASSERT_EQUAL_INT(-1, sigprocmask(99, &requested, NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_sigpending_reports_kernel_pending_state(void)
{
    sigset_t pending = UINT32_MAX;
    pending_mask = Signal__mask(SIGTERM);

    TEST_ASSERT_EQUAL_INT(0, sigpending(&pending));
    TEST_ASSERT_EQUAL_UINT32(Signal__mask(SIGTERM), pending);
    TEST_ASSERT_EQUAL_INT(-1, sigpending(NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_sigsuspend_restores_the_mask_and_reports_interruption(void)
{
    sigset_t blocked = Signal__mask(SIGTERM);
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, &blocked, NULL));
    sigset_t temporary = Signal__mask(SIGINT);

    TEST_ASSERT_EQUAL_INT(-1, sigsuspend(&temporary));
    TEST_ASSERT_EQUAL_INT(EINTR, errno);
    sigset_t restored;
    TEST_ASSERT_EQUAL_INT(0, sigprocmask(SIG_SETMASK, NULL, &restored));
    TEST_ASSERT_EQUAL_UINT32(blocked, restored);
    TEST_ASSERT_EQUAL_INT(-1, sigsuspend(NULL));
    TEST_ASSERT_EQUAL_INT(EINVAL, errno);
}

void test_abort_raises_sigabrt_then_uses_nonreturning_fallbacks(void)
{
    if (setjmp(abort_return) == 0)
    {
        abort();
    }

    TEST_ASSERT_EQUAL_UINT(2, kill_requests);
    TEST_ASSERT_EQUAL_UINT64(SIGABRT, first_requested_signal);
    TEST_ASSERT_EQUAL_UINT64(SIGKILL, requested_signal);
    TEST_ASSERT_EQUAL_INT(128 + SIGABRT, abort_fallback_code);
}

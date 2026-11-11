#include "synchronization.h"
#include "sys/process.h"
#include "sys/process_sleep.h"
#include "sys/print.h"
#include "sys/thread_wait.h"
#include "sys/user_mutex.h"
#include "types/fixed_array.h"

#include <signal.h>
#include <unistd.h>

#define PROBE_WORKERS 4
#define PROBE_ITERATIONS 32

typedef ThreadID * ThreadIDPtr;
OptionalDef(ThreadID);

static bool ThreadID__equals(void * self, void * other)
{
    return *(ThreadIDPtr)self == *(ThreadIDPtr)other;
}

FixedArrayDefine(ThreadID, PROBE_WORKERS, ProbeThreads, ThreadID__equals);
FixedArrayStackBodyDefine(ProbeThreads);

static UserMutex counter_mutex;
static uint64_t protected_counter;
static ThreadWaitWord started;
static ThreadWaitWord signal_word;
static volatile sig_atomic_t signals_seen;

static bool ThreadProbe__join(ThreadCreateResult id, uint64_t expected)
{
    if (id <= 0)
    {
        return false;
    }
    match_result(
        thread_join((ThreadID)id),
        success(result)
        {
            return result.value == expected;
        },
        error(code, message)
        {
            return false;
        }
    );
    return false;
}

static void * ThreadProbe__counter_worker(void * argument)
{
    atomic_fetch_add_explicit(&started, 1, memory_order_release);
    if (thread_wake(&started, 1) < 0)
    {
        return (void *)(uintptr_t)1;
    }
    for (unsigned iteration = 0; iteration < PROBE_ITERATIONS; iteration++)
    {
        if (UserMutex__lock(&counter_mutex) != THREAD_WAIT_WOKEN)
        {
            return (void *)(uintptr_t)2;
        }
        uint64_t previous = protected_counter;
        sleep_usec(1000);
        protected_counter = previous + 1;
        if (UserMutex__unlock(&counter_mutex) != THREAD_WAIT_WOKEN)
        {
            return (void *)(uintptr_t)3;
        }
    }
    return argument;
}

static bool ThreadProbe__contention(void)
{
    counter_mutex = UserMutex__new();
    if (UserMutex__lock(&counter_mutex) != THREAD_WAIT_WOKEN)
    {
        return false;
    }
    ProbeThreads workers = ProbeThreads__new();
    for (unsigned index = 0; index < PROBE_WORKERS; index++)
    {
        ThreadCreateResult worker = thread_create(ThreadProbe__counter_worker, (void *)(uintptr_t)42);
        if (worker <= 0)
        {
            return false;
        }
        match_result(
            ProbeThreads__push(&workers, (ThreadID)worker),
            success_do_nothing(),
            error(code, message)
            {
                return false;
            }
        );
    }
    while (atomic_load_explicit(&started, memory_order_acquire) != PROBE_WORKERS)
    {
        ThreadWaitValue observed = atomic_load_explicit(&started, memory_order_acquire);
        if (observed == PROBE_WORKERS)
        {
            break;
        }
        ThreadWaitStatus status = thread_wait(&started, observed, 1000000);
        if (status != THREAD_WAIT_WOKEN && status != THREAD_WAIT_CHANGED)
        {
            return false;
        }
    }
    sleep_usec(10000);
    if (UserMutex__unlock(&counter_mutex) != THREAD_WAIT_WOKEN)
    {
        return false;
    }
    for_each_fa(
        ProbeThreads,
        workers,
        index,
        worker,
        {
            if (!ThreadProbe__join((ThreadCreateResult)worker, 42))
            {
                return false;
            }
        }
    );
    if (UserMutex__lock(&counter_mutex) != THREAD_WAIT_WOKEN)
    {
        return false;
    }
    bool counted = protected_counter == PROBE_WORKERS * PROBE_ITERATIONS;
    return UserMutex__unlock(&counter_mutex) == THREAD_WAIT_WOKEN && counted;
}

static void ThreadProbe__signal_handler(int signal)
{
    signals_seen++;
}

static void * ThreadProbe__send_signal(void * argument)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigprocmask(SIG_BLOCK, &mask, &mask) != 0)
    {
        return (void *)(uintptr_t)1;
    }

    sleep_usec(10000);
    if (kill_process((int64_t)process_current_pid(), PROCESS_SIGNAL_USER_1) != PROCESS_SIGNAL_OK)
    {
        return (void *)(uintptr_t)2;
    }
    return argument;
}

static bool ThreadProbe__interruption(void)
{
    struct sigaction action = SignalAction__new(ThreadProbe__signal_handler, SA_RESTART);

    struct sigaction previous;
    sigset_t mask;
    sigset_t previous_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR1);
    if (sigaction(SIGUSR1, &action, &previous) != 0 || sigprocmask(SIG_UNBLOCK, &mask, &previous_mask) != 0)
    {
        return false;
    }

    ThreadCreateResult sender = thread_create(ThreadProbe__send_signal, (void *)(uintptr_t)42);
    if (sender <= 0)
    {
        return false;
    }
    ThreadWaitStatus status = thread_wait(&signal_word, 0, 1000000);
    bool joined = ThreadProbe__join(sender, 42);
    bool restored = sigaction(SIGUSR1, &previous, &action) == 0 && sigprocmask(SIG_SETMASK, &previous_mask, &mask) == 0;
    return joined && restored && status == THREAD_WAIT_INTERRUPTED && signals_seen == 1 &&
           thread_wake(&signal_word, UINT64_MAX) == 0;
}

static void ThreadProbe__exit_handler(int signal)
{
    thread_exit((void *)(uintptr_t)42);
}

static void * ThreadProbe__exit_waiter(void * argument)
{
    sigset_t mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    if (sigprocmask(SIG_UNBLOCK, &mask, &mask) != 0)
    {
        return (void *)(uintptr_t)1;
    }

    atomic_store_explicit(&started, 1, memory_order_release);
    thread_wake(&started, 1);
    thread_wait(&signal_word, 0, THREAD_WAIT_INDEFINITE);
    return argument;
}

static bool ThreadProbe__exit_cleanup(void)
{
    struct sigaction action = SignalAction__new(ThreadProbe__exit_handler, 0);

    struct sigaction previous;
    sigset_t mask;
    sigset_t previous_mask;
    sigemptyset(&mask);
    sigaddset(&mask, SIGUSR2);
    if (sigaction(SIGUSR2, &action, &previous) != 0 || sigprocmask(SIG_BLOCK, &mask, &previous_mask) != 0)
    {
        return false;
    }

    atomic_store_explicit(&started, 0, memory_order_release);
    ThreadCreateResult waiter = thread_create(ThreadProbe__exit_waiter, (void *)(uintptr_t)1);
    if (waiter <= 0)
    {
        return false;
    }
    while (atomic_load_explicit(&started, memory_order_acquire) == 0)
    {
        ThreadWaitStatus status = thread_wait(&started, 0, 1000000);
        if (status != THREAD_WAIT_WOKEN && status != THREAD_WAIT_CHANGED)
        {
            return false;
        }
    }
    sleep_usec(10000);
    if (kill_process((int64_t)process_current_pid(), PROCESS_SIGNAL_USER_2) != PROCESS_SIGNAL_OK)
    {
        return false;
    }
    bool joined = ThreadProbe__join(waiter, 42);
    bool restored = sigaction(SIGUSR2, &previous, &action) == 0 && sigprocmask(SIG_SETMASK, &previous_mask, &mask) == 0;
    return joined && restored && thread_wake(&signal_word, UINT64_MAX) == 0;
}

int ThreadProbe__synchronization(void)
{
    ThreadWaitWord word = 7;
    if (thread_wait(&word, 8, 0) != THREAD_WAIT_CHANGED || thread_wait(&word, 7, 0) != THREAD_WAIT_TIMED_OUT ||
        thread_wait(&word, 7, 2000) != THREAD_WAIT_TIMED_OUT)
    {
        IO__print(&bstr("thread_probe: wait status failed\n"));
        return 1;
    }
    if (!ThreadProbe__contention())
    {
        IO__print(&bstr("thread_probe: mutex contention failed\n"));
        return 1;
    }
    if (!ThreadProbe__interruption())
    {
        IO__print(&bstr("thread_probe: wait interruption failed\n"));
        return 1;
    }
    if (!ThreadProbe__exit_cleanup())
    {
        IO__print(&bstr("thread_probe: wait exit cleanup failed\n"));
        return 1;
    }
    IO__print(&bstr("thread_probe: sync ok\n"));
    return 0;
}

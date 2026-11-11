#include "allocation.h"
#include "sys/process.h"
#include "sys/process_sleep.h"
#include "sys/print.h"
#include "sys/thread_wait.h"
#include "newlib/allocator.h"
#include "types/fixed_array.h"

#include <malloc.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#define ALLOCATION_WORKERS 4
#define ALLOCATION_ITERATIONS 128

typedef ThreadID * ThreadIDPtr;
OptionalDef(ThreadID);

static bool ThreadID__equals(void * self, void * other)
{
    return *(ThreadIDPtr)self == *(ThreadIDPtr)other;
}

FixedArrayDefine(ThreadID, ALLOCATION_WORKERS, AllocationWorkers, ThreadID__equals);
FixedArrayStackBodyDefine(AllocationWorkers);

static ThreadWaitWord allocation_gate;

static bool ThreadProbe__pattern(const uint8_t * allocation, size_t size, uint8_t pattern)
{
    for (size_t index = 0; index < size; index++)
    {
        if (allocation[index] != pattern)
        {
            return false;
        }
    }
    return true;
}

static void * ThreadProbe__allocate(void * argument)
{
    while (atomic_load_explicit(&allocation_gate, memory_order_acquire) == 0)
    {
        ThreadWaitStatus status = thread_wait(&allocation_gate, 0, THREAD_WAIT_INDEFINITE);
        if (status != THREAD_WAIT_WOKEN && status != THREAD_WAIT_CHANGED && status != THREAD_WAIT_INTERRUPTED)
        {
            return (void *)(uintptr_t)6;
        }
    }
    uint8_t pattern = (uint8_t)(uintptr_t)argument;
    for (unsigned iteration = 0; iteration < ALLOCATION_ITERATIONS; iteration++)
    {
        size_t size = 17 + (iteration * 37 + pattern) % 2048;
        uint8_t * allocation = calloc(size, 1);
        if (!allocation || !ThreadProbe__pattern(allocation, size, 0))
        {
            free(allocation);
            return (void *)(uintptr_t)1;
        }
        memset(allocation, pattern, size);
        sleep_usec(100);
        uint8_t * resized = realloc(allocation, size * 2);
        if (!resized)
        {
            free(allocation);
            return (void *)(uintptr_t)2;
        }
        allocation = resized;
        if (!ThreadProbe__pattern(allocation, size, pattern) || malloc_usable_size(allocation) < size * 2)
        {
            free(allocation);
            return (void *)(uintptr_t)3;
        }
        size_t alignment = (size_t)1 << (4 + iteration % 6);
        uint8_t * aligned = memalign(alignment, size);
        if (!aligned || (uintptr_t)aligned % alignment != 0)
        {
            free(aligned);
            free(allocation);
            return (void *)(uintptr_t)4;
        }
        memset(aligned, pattern, size);
        sleep_usec(100);
        bool intact = ThreadProbe__pattern(aligned, size, pattern) && ThreadProbe__pattern(allocation, size, pattern);
        free(aligned);
        free(allocation);
        if (!intact)
        {
            return (void *)(uintptr_t)5;
        }
    }
    return (void *)(uintptr_t)42;
}

static bool ThreadProbe__allocator_fork(void)
{
    uint8_t * allocation = malloc(257);
    if (!allocation)
    {
        return false;
    }
    memset(allocation, 93, 257);
    pid_t child = fork();
    if (child == 0)
    {
        bool intact = ThreadProbe__pattern(allocation, 257, 93);
        free(allocation);
        void * fresh = memalign(256, 512);
        bool allocated = fresh && (uintptr_t)fresh % 256 == 0;
        free(fresh);
        _exit(intact && allocated ? 0 : 1);
    }
    bool intact = ThreadProbe__pattern(allocation, 257, 93);
    free(allocation);
    if (child < 0)
    {
        return false;
    }
    int status;
    return waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0 && intact;
}

int ThreadProbe__allocation(void)
{
    AllocationWorkers workers = AllocationWorkers__new();
    for (unsigned index = 0; index < ALLOCATION_WORKERS; index++)
    {
        ThreadCreateResult worker = thread_create(ThreadProbe__allocate, (void *)(uintptr_t)(index + 1));
        if (worker <= 0)
        {
            return 1;
        }
        match_result(
            AllocationWorkers__push(&workers, (ThreadID)worker),
            success_do_nothing(),
            error(code, message)
            {
                return 1;
            }
        );
    }
    Allocator__lock();
    atomic_store_explicit(&allocation_gate, 1, memory_order_release);
    ThreadWakeResult woken = thread_wake(&allocation_gate, ALLOCATION_WORKERS);
    sleep_usec(10000);
    Allocator__unlock();
    if (woken < 0)
    {
        return 1;
    }
    for_each_fa(
        AllocationWorkers,
        workers,
        index,
        worker,
        {
            match_result(
                thread_join(worker),
                success(joined)
                {
                    if (joined.value != 42)
                    {
                        return 1;
                    }
                },
                error(code, message)
                {
                    return 1;
                }
            );
        }
    );
    if (!ThreadProbe__allocator_fork())
    {
        return 1;
    }
    IO__print(&bstr("thread_probe: allocation ok\n"));
    return 0;
}

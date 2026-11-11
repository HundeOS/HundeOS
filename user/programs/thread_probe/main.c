#include <stdint.h>
#include <errno.h>
#include <stdlib.h>

#include "sys/process.h"
#include "sys/print.h"
#include "sys/process_sleep.h"
#include "synchronization.h"
#include "allocation.h"

static _Thread_local volatile uintptr_t thread_value = 41;
static _Thread_local _Alignas(64) volatile uintptr_t thread_zero;
static uintptr_t worker_addresses[2];

static void * ThreadProbe__run(void * argument)
{
    uintptr_t value = (uintptr_t)argument;
    if (thread_value != 41 || thread_zero != 0 || errno != 0 || (uintptr_t)&thread_zero % 64 != 0)
    {
        return (void *)(uintptr_t)1;
    }
    worker_addresses[value == 41 ? 0 : 1] = (uintptr_t)&thread_value;
    const char * end;
    if (strtol("999999999999999999999999999999999999999", (char **)&end, 10) == 0 || *end != '\0' || errno != ERANGE)
    {
        return (void *)(uintptr_t)2;
    }
    thread_value = value;
    thread_zero = value + 10;
    errno = (int)value;
    for (unsigned iteration = 0; iteration < 8; iteration++)
    {
        for (unsigned spin = 0; spin < 100000; spin++)
        {
            if (thread_value != value || thread_zero != value + 10 || errno != (int)value)
            {
                return (void *)(uintptr_t)3;
            }
        }
        sleep_usec(1000);
        if (thread_value != value || thread_zero != value + 10 || errno != (int)value)
        {
            return (void *)(uintptr_t)3;
        }
    }
    return (void *)((uintptr_t)argument + 1);
}

static void * ThreadProbe__run_after_main_exit(void * argument)
{
    if ((uintptr_t)ThreadProbe__run((void *)(uintptr_t)41) != 42)
    {
        IO__print(&bstr("thread_probe: main-exit TLS failed\n"));
        return (void *)(uintptr_t)1;
    }
    IO__print(&bstr("thread_probe: main-exit ok\n"));
    return argument;
}

int main(int argc, char ** argv)
{
    if (argc > 1)
    {
        BString mode = BString__new(argv[1]);
        if (BString__equals(&mode, &bstr("allocation")))
        {
            return ThreadProbe__allocation();
        }
        if (BString__equals(&mode, &bstr("sync")))
        {
            return ThreadProbe__synchronization();
        }
        if (BString__equals(&mode, &bstr("main-exit")))
        {
            if (thread_create(ThreadProbe__run_after_main_exit, (void *)(uintptr_t)1) <= 0)
            {
                IO__print(&bstr("thread_probe: main-exit create failed\n"));
                return 1;
            }
            thread_exit((void *)(uintptr_t)1);
        }
    }
    if (thread_value != 41 || thread_zero != 0 || errno != 0)
    {
        IO__print(&bstr("thread_probe: initial TLS failed\n"));
        return 1;
    }
    thread_value = 999;
    thread_zero = 777;
    errno = EIO;
    ThreadCreateResult id = thread_create(ThreadProbe__run, (void *)(uintptr_t)41);
    ThreadCreateResult second_id = thread_create(ThreadProbe__run, (void *)(uintptr_t)81);
    if (id <= 0 || second_id <= 0)
    {
        IO__print(&bstr("thread_probe: create failed\n"));
        return 1;
    }
    match_result(
        thread_join((ThreadID)id),
        success(joined)
        {
            if (joined.value != 42)
            {
                IO__print(&bstr("thread_probe: wrong result\n"));
                return 1;
            }
        },
        error(code, message)
        {
            IO__print(&bstr("thread_probe: join failed\n"));
            return 1;
        }
    );
    match_result(
        thread_join((ThreadID)second_id),
        success(joined)
        {
            if (joined.value != 82)
            {
                IO__print(&bstr("thread_probe: second TLS failed\n"));
                return 1;
            }
        },
        error(code, message)
        {
            IO__print(&bstr("thread_probe: second join failed\n"));
            return 1;
        }
    );
    if (thread_value != 999 || thread_zero != 777 || errno != EIO || worker_addresses[0] == worker_addresses[1] ||
        worker_addresses[0] == (uintptr_t)&thread_value || worker_addresses[1] == (uintptr_t)&thread_value)
    {
        IO__print(&bstr("thread_probe: TLS isolation failed\n"));
        return 1;
    }
    IO__print(&bstr("thread_probe: TLS ok\n"));
    IO__print(&bstr("thread_probe: ok\n"));
    return 0;
}

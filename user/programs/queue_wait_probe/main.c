#include "ipc/ipc_platform.h"
#include "ipc/queue_wait.h"
#include "ipc/queue_wait_control.h"
#include "sys/syscall.h"
#include "sys/print.h"
#include "sys/process.h"
#include "syscalls/service.h"
#include "sys/process_metrics.h"

#define WAIT_PROBE_SAMPLES 16ULL

typedef struct WaitProbeSummary
{
    uint64_t timeouts;
    uint64_t activity;
    uint64_t failures;
    uint64_t timeout_usec;
    uint64_t min_timeout_usec;
    uint64_t max_timeout_usec;
    uint64_t call_usec;
} WaitProbeSummary;

typedef WaitProbeSummary * WaitProbeSummaryPtr;

static WaitProbeSummary WaitProbeSummary__new(void)
{
    return (WaitProbeSummary){0};
}

static void WaitProbeSummary__record(WaitProbeSummaryPtr self, SystemCallResult result, uint64_t elapsed)
{
    self->call_usec += elapsed;
    if (result == QUEUE_WAIT_TIMEOUT)
    {
        if (self->timeouts == 0 || elapsed < self->min_timeout_usec)
        {
            self->min_timeout_usec = elapsed;
        }
        if (elapsed > self->max_timeout_usec)
        {
            self->max_timeout_usec = elapsed;
        }
        self->timeouts++;
        self->timeout_usec += elapsed;
    }
    else if (result == QUEUE_WAIT_CHANGED)
    {
        self->activity++;
    }
    else
    {
        self->failures++;
    }
}

int main(int argc, char ** argv)
{
    uint64_t ticks = QUEUE_WAIT_DEFAULT_TICKS;
    if (argc > 2)
    {
        return 1;
    }
    if (argc == 2)
    {
        BString argument = BString__new(argv[1]);
        match_optional(BString__parse_uint64(&argument),
            some(value)
            {
                ticks = value;
            },
            none()
            {
                return 1;
            }
        );
    }
    if (ticks == 0 || ticks > QUEUE_WAIT_MAX_TICKS)
    {
        return 1;
    }
    ConstProcessRegistryPtr registry;
    match_result(
        process_registry_map(),
        success(mapped)
        {
            registry = mapped;
        },
        error(code, message)
        {
            return 1;
        }
    );
    IO__printf(&bstr("queue_wait_probe: pid=%llu ticks=%llu samples=%llu\n"), process_current_pid(), ticks,
               WAIT_PROBE_SAMPLES);
    SystemCallResult control_address = syscall(SERVICE_CALL, SERVICE_QUEUE_WAIT_CONTROL, 0, 0, 0, 0);
    if ((int64_t)control_address < 0)
    {
        return 1;
    }
    QueueWaitControlPtr control = (QueueWaitControlPtr)control_address;
    WaitProbeSummary summary = WaitProbeSummary__new();
    for (uint64_t sample = 0; sample < WAIT_PROBE_SAMPLES; sample++)
    {
        IpcEventGeneration observed =
            IpcPlatform__prepare_wait(IpcDoorbell__event(IpcPlatform__local_doorbell()));
        uint64_t start = IpcPlatform__monotonic_usec();
        QueueWaitControl__begin(control);
        QueueWaitControl__commit(control, start, QUEUE_WAIT_NO_DEADLINE, observed);
        SystemCallResult result = syscall(SERVICE_CALL, SERVICE_QUEUE_WAIT, observed, ticks, 0, 0);
        uint64_t elapsed = IpcPlatform__monotonic_usec() - start;
        QueueWaitControl__complete_wait(control, start + elapsed);
        WaitProbeSummary__record(&summary, result, elapsed);
    }
    IO__printf(&bstr("timeouts=%llu activity=%llu failures=%llu timeout_call_usec: min=%llu avg=%llu max=%llu\n"),
               summary.timeouts, summary.activity, summary.failures, summary.min_timeout_usec,
               summary.timeouts == 0 ? 0 : summary.timeout_usec / summary.timeouts, summary.max_timeout_usec);
    match_result(
        ProcessMetrics__snapshot_after(registry, process_current_pid(), IpcPlatform__monotonic_usec()),
        success(record)
        {
            ProcessQueueWaitUsage usage = record.queue_wait_usage;
            IO__printf(&bstr("cpu=%llu call_total_usec=%llu kernel_parks=%llu kernel_timeouts=%llu "
                             "kernel_activity=%llu kernel_selections=%llu speculative_selections=%llu\n"
                             "kernel_parked_usec=%llu kernel_runnable_usec=%llu\n"),
                       record.cpu, summary.call_usec, usage.parks, usage.timeout_wakes, usage.activity_wakes,
                       usage.selections, usage.speculative_selections, usage.parked_usec, usage.runnable_usec);
        },
        error(code, message)
        {
            IO__printf(&bstr("kernel counters unavailable: %B\n"), &message);
        }
    );
    return summary.failures == 0 && summary.timeouts != 0 ? 0 : 1;
}

#include "../../sys/io.h"
#include "../../sys/process_sleep.h"
#include "../../sys/syslog.h"
#include "../../user.h"

#ifndef HUMPEN_WACHHUND_ENTRYPOINT
#define HUMPEN_WACHHUND_ENTRYPOINT main
#endif

#define HUMPEN_WACHHUND_ARG_COUNT 4
#define HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC 10000ULL
#define HUMPEN_WACHHUND_MAX_BACKOFF_USEC 1000000ULL

static const char * HUMPEN_WACHHUND_LOG_TAG = "humpen-wachhund";

static uint64_t HumpenWachhund__next_backoff(uint64_t current)
{
    if (current >= HUMPEN_WACHHUND_MAX_BACKOFF_USEC / 2ULL)
    {
        return HUMPEN_WACHHUND_MAX_BACKOFF_USEC;
    }

    return current * 2ULL;
}

int HUMPEN_WACHHUND_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != HUMPEN_WACHHUND_ARG_COUNT)
    {
        kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "usage <device> <filesystem> <mountpoint>");
        return 1;
    }

    BString device_path = BString__new(argv[1]);
    BString filesystem_type = BString__new(argv[2]);
    BString mountpoint = BString__new(argv[3]);
    uint64_t backoff_usec = HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC;

    while (true)
    {
        IOStatus status = IO__mount_device(&device_path, &filesystem_type, &mountpoint);
        if (status == IO_OK)
        {
            kernel_syslog_tagf(
                HUMPEN_WACHHUND_LOG_TAG,
                "mounted %s on %s",
                BString__cstr(&device_path),
                BString__cstr(&mountpoint)
            );
            return 0;
        }

        kernel_syslog_tagf(
            HUMPEN_WACHHUND_LOG_TAG,
            "failed to mount %s on %s: %i",
            BString__cstr(&device_path),
            BString__cstr(&mountpoint),
            (int) status
        );

        ProcessSleepResult sleep_result = sleep_usec(backoff_usec);
        if (sleep_result != PROCESS_SLEEP_OK)
        {
            kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "sleep failed: %i", (int) sleep_result);
            return 1;
        }

        backoff_usec = HumpenWachhund__next_backoff(backoff_usec);
    }
}

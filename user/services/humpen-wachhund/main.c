#include "sys/io.h"
#include "sys/process_sleep.h"
#include "sys/spawn.h"
#include "sys/syslog.h"
#include "../../user.h"

#ifndef HUMPEN_WACHHUND_ENTRYPOINT
#define HUMPEN_WACHHUND_ENTRYPOINT main
#endif

#define HUMPEN_WACHHUND_ARG_COUNT 4
#define HUMPEN_WACHHUND_INITIAL_BACKOFF_USEC 10000ULL
#define HUMPEN_WACHHUND_MAX_BACKOFF_USEC 1000000ULL
#define HUMPEN_WACHHUND_EXT2FS_PATH "/sbin/drivers/ext2fs"
#define HUMPEN_WACHHUND_FAT32FS_PATH "/sbin/drivers/fat32fs"

static const char * HUMPEN_WACHHUND_LOG_TAG = "humpen-wachhund";
static BString humpen_wachhund_ext2_filesystem = bstr("ext2");
static BString humpen_wachhund_fat32_filesystem = bstr("fat32");
static BString humpen_wachhund_ext2fs_path = bstr(HUMPEN_WACHHUND_EXT2FS_PATH);
static BString humpen_wachhund_fat32fs_path = bstr(HUMPEN_WACHHUND_FAT32FS_PATH);

static uint64_t HumpenWachhund__next_backoff(uint64_t current)
{
    if (current >= HUMPEN_WACHHUND_MAX_BACKOFF_USEC / 2ULL)
    {
        return HUMPEN_WACHHUND_MAX_BACKOFF_USEC;
    }

    return current * 2ULL;
}

static Result(SpawnResult) HumpenWachhund__mount_ext2(ConstBStringPtr device_path, ConstBStringPtr mountpoint)
{
    return OS__spawn(P_WAIT, &humpen_wachhund_ext2fs_path, &Some(BString, *device_path), &Some(BString, *mountpoint),
                     &SPAWN_NO_ARG);
}

static Result(SpawnResult) HumpenWachhund__mount_fat32(ConstBStringPtr device_path, ConstBStringPtr mountpoint)
{
    return OS__spawn(P_WAIT, &humpen_wachhund_fat32fs_path, &Some(BString, *device_path), &Some(BString, *mountpoint),
                     &SPAWN_NO_ARG);
}

static Result(SpawnResult)
    HumpenWachhund__mount_userspace_filesystem(ConstBStringPtr device_path, ConstBStringPtr filesystem_type,
                                               ConstBStringPtr mountpoint)
{
    if (BString__equals(filesystem_type, &humpen_wachhund_ext2_filesystem))
    {
        return HumpenWachhund__mount_ext2(device_path, mountpoint);
    }

    if (BString__equals(filesystem_type, &humpen_wachhund_fat32_filesystem))
    {
        return HumpenWachhund__mount_fat32(device_path, mountpoint);
    }

    return Error(SpawnResult, bstr("not a userspace filesystem"), SPAWN_NO_FILE);
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
        if (BString__equals(&filesystem_type, &humpen_wachhund_ext2_filesystem) ||
            BString__equals(&filesystem_type, &humpen_wachhund_fat32_filesystem))
        {
            match_result(
                HumpenWachhund__mount_userspace_filesystem(&device_path, &filesystem_type, &mountpoint),
                success(spawn_result)
                {
                    if (spawn_result == 0)
                    {
                        kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "mounted %B on %B", &device_path, &mountpoint);
                        return 0;
                    }

                    kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "failed to mount %B on %B: %i", &device_path,
                                       &mountpoint, (int)spawn_result);
                },
                error(code, message)
                {
                    kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "failed to start %B fs for %B on %B: %B (%i)",
                                       &filesystem_type, &device_path, &mountpoint, &message, (int)code);
                }
            );
        }
        else
        {
            IOStatus status = IO__mount_device(&device_path, &filesystem_type, &mountpoint);
            if (status == IO_OK)
            {
                kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "mounted %B on %B", &device_path, &mountpoint);
                return 0;
            }

            kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "failed to mount %B on %B: %i", &device_path, &mountpoint,
                               (int)status);
        }

        ProcessSleepResult sleep_result = sleep_usec(backoff_usec);
        if (sleep_result != PROCESS_SLEEP_OK)
        {
            kernel_syslog_tagf(HUMPEN_WACHHUND_LOG_TAG, "sleep failed: %i", (int)sleep_result);
            return 1;
        }

        backoff_usec = HumpenWachhund__next_backoff(backoff_usec);
    }
}

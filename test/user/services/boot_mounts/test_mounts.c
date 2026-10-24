#include "unity.h"
#include <string.h>
#include "../../../../user/services/hundeschule/mounts.h"
#include "boot/services.h"
#include "sys/io.h"
#include "sys/service.h"
static const char * fstab;
static unsigned registered;
static unsigned published;
static unsigned started;
static unsigned closed;
static bool root;
static bool fail_start;
static bool short_reply;
static bool fail_tmp_ready;
static bool fail_proc_ready;
static bool fail_dev_ready;
static bool hosted_channel;
static bool hosted_started;
static bool fail_hosted_ready;
static bool fail_hosted_mount;
static bool fail_root_publication;
static bool block_device_waited;
static bool fail_block_device_ready;
static bool console_input_waited;
static bool console_output_waited;
static bool fail_console_output;

void setUp(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio4 ext2 /mnt/sd\n";
    registered = started = closed = 0;
    published = 0;
    fail_dev_ready = false;
    console_input_waited = console_output_waited = fail_console_output = false;
    root = fail_start = short_reply = false;
    fail_tmp_ready = false;
    fail_proc_ready = false;
    hosted_channel = hosted_started = fail_hosted_ready = false;
    fail_hosted_mount = fail_root_publication = false;
    block_device_waited = fail_block_device_ready = false;
}

void tearDown(void)
{
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config/fstab"), *path);
    return Success(IOHandle, 7);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    return Success(IOStatus, IO_OK);
}

Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos capacity)
{
    DynString text;
    match_result(DynString__init(&text, capacity),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(DynString__append_cstr(&text, fstab),
        success_do_nothing(),
        error_test_fail()
    );
    return Success(DynString, text);
}

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr name)
{
    hosted_channel = BString__equals(name, &bstr(IO_HOST_MOUNT_SERVICE));
    return Success(Channel, ((Channel){0}));
}

ChannelCode Channel__close(ChannelPtr channel)
{
    closed++;
    hosted_channel = false;
    return CHANNEL_OK;
}

Result(ChannelReplySize) Channel__submit(ChannelPtr channel, ConstChannelDataPtr data, size_t size,
                                         ChannelDataPtr reply_data, size_t capacity)
{
    TEST_ASSERT_EQUAL(sizeof(IORequest), size);
    IORequest request;
    memcpy(&request, data, sizeof(request));
    TEST_ASSERT_EQUAL(IO_REQUEST_MOUNT, request.operation);
    BString line = IORequest__path(&request);
    IOReply reply = IOReply__new(IO_OK, 0);
    match_result(
        MountConfig__parse(&line),
        success(config)
        {
            if (BString__equals(&config.filesystem_type, &bstr("ext2.wuff")) ||
                BString__equals(&config.filesystem_type, &bstr("fat32.wuff")) ||
                BString__equals(&config.filesystem_type, &bstr("console")))
            {
                if (hosted_channel)
                {
                    hosted_started = !fail_hosted_mount;
                    if (fail_hosted_mount)
                    {
                        reply.status = IO_INVALID_PATH;
                    }
                }
                else
                {
                    TEST_ASSERT_TRUE(hosted_started);
                }
            }
            if (!hosted_channel && BString__equals(&config.mountpoint, &bstr("/")))
            {
                if (root || fail_root_publication)
                {
                    reply.status = IO_INVALID_MOUNT_POINT;
                }
                root = reply.status == IO_OK;
            }
            if (!hosted_channel && !BString__equals(&config.mountpoint, &bstr("/")))
            {
                TEST_ASSERT_TRUE(root);
            }
        },
        error_test_fail()
    );
    memcpy(reply_data, &reply, sizeof(reply));
    registered++;
    if (!hosted_channel)
    {
        published++;
    }
    return Success(ChannelReplySize, short_reply ? sizeof(reply) - 1 : sizeof(reply));
}

Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0,
                              OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_ASSERT_TRUE(root);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/humpen-wachhund"), *path);
    match_optional(*arg1,
        some(value)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("ext2"), value);
        },
        none_test_fail()
    );
    match_optional(*arg2,
        some(value)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("/"), value);
        },
        none_test_fail()
    );
    started++;
    return Success(SpawnResult, 42);
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    if (BString__equals(name, &bstr("uart_rx")))
    {
        console_input_waited = true;
    }
    if (BString__equals(name, &bstr("uart_tx")))
    {
        console_output_waited = true;
        if (fail_console_output)
        {
            return SERVICE_SCHEDULER_ERROR;
        }
    }
    if (BString__equals(name, &bstr("/dev/mmc0")))
    {
        block_device_waited = true;
        if (fail_block_device_ready)
        {
            return SERVICE_SCHEDULER_ERROR;
        }
    }
    if (fail_hosted_ready && BString__equals(name, &bstr(IO_HOST_MOUNT_SERVICE)))
    {
        return SERVICE_PERMISSION_DENIED;
    }
    if ((fail_dev_ready && BString__equals(name, &bstr("/dev"))) ||
        (fail_tmp_ready && BString__equals(name, &bstr("/tmp"))) ||
        (fail_proc_ready && BString__equals(name, &bstr("/proc"))))
    {
        return SERVICE_SCHEDULER_ERROR;
    }
    return fail_start ? SERVICE_SCHEDULER_ERROR : SERVICE_OK;
}

Result(ServiceCode)
    BootService__start(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    started++;
    TEST_ASSERT_FALSE(BString__equals(path, &bstr("/sbin/drivers/memfs")));
    TEST_ASSERT_FALSE(BString__equals(path, &bstr("/sbin/drivers/procfs")));
    TEST_ASSERT_FALSE(BString__equals(path, &bstr("/sbin/drivers/devfs")));
    return fail_start ? Error(ServiceCode, bstr("failed"), SERVICE_SCHEDULER_ERROR) : Success(ServiceCode, SERVICE_OK);
}

void test_later_mounts_register_root_and_devices_after_reading_boot_config(void)
{
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(5, registered);
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_missing_root_and_duplicate_root_fail_and_close_control_channel(void)
{
    fstab = "device /dev/virtio4 ext2 /mnt/sd\n";
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio1 ext2 /\n";
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(2, closed);
    TEST_ASSERT_EQUAL(0, registered);
}

void test_hosted_memfs_readiness_failure_stops_before_other_builtin_drivers(void)
{
    fail_tmp_ready = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(2, registered);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_hosted_procfs_readiness_failure_stops_mount_publication(void)
{
    fail_proc_ready = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(4, registered);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_hosted_devfs_readiness_failure_starts_no_device_filesystem_process(void)
{
    fstab = "device /dev/virtio1 ext2.wuff /\n";
    fail_dev_ready = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(2, published);
    TEST_ASSERT_EQUAL(3, registered);
    TEST_ASSERT_EQUAL(2, closed);
}

void test_mount_readiness_failure_stops_later_services(void)
{
    fail_start = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_short_mount_reply_is_rejected_before_starting_driver(void)
{
    short_reply = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_hosted_mount_uses_control_channel_before_namespace_publication(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio7 ext2.wuff /mnt/hosted-ext2\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_EQUAL(6, registered);
    TEST_ASSERT_EQUAL(2, closed);
    TEST_ASSERT_EQUAL(1, started);
}

void test_hosted_mount_control_failure_prevents_namespace_publication(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio7 ext2.wuff /mnt/hosted-ext2\n";
    fail_hosted_ready = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_FALSE(hosted_started);
    TEST_ASSERT_EQUAL(1, registered);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_console_mount_and_default_alias_use_host_control_without_a_watchdog(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice uart_rx,uart_tx console /dev/tty0\n"
            "device /dev/tty0 console :console:\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_TRUE(console_input_waited);
    TEST_ASSERT_TRUE(console_output_waited);
    TEST_ASSERT_FALSE(block_device_waited);
    TEST_ASSERT_EQUAL(7, registered);
    TEST_ASSERT_EQUAL(3, closed);
    TEST_ASSERT_EQUAL(1, started);
}

void test_console_failure_prevents_node_publication_and_following_alias(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice uart_rx,uart_tx console /dev/tty0\n"
            "device /dev/tty0 console :console:\n";
    fail_hosted_mount = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_FALSE(hosted_started);
    TEST_ASSERT_EQUAL(2, registered);
    TEST_ASSERT_EQUAL(1, published);
    TEST_ASSERT_EQUAL(2, closed);
}

void test_console_output_readiness_failure_prevents_initialization_or_publication(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice uart_rx,uart_tx console /dev/tty0\n";
    fail_console_output = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_TRUE(console_input_waited);
    TEST_ASSERT_TRUE(console_output_waited);
    TEST_ASSERT_FALSE(hosted_started);
    TEST_ASSERT_EQUAL(1, registered);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_fat32_wuff_mount_uses_host_control_without_legacy_driver(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio3 fat32.wuff /mnt/fat\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_EQUAL(6, registered);
    TEST_ASSERT_EQUAL(2, closed);
    TEST_ASSERT_EQUAL(1, started);
}

void test_fat32_wuff_control_failure_prevents_namespace_publication(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio3 fat32.wuff /mnt/fat\n";
    fail_hosted_mount = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_FALSE(hosted_started);
    TEST_ASSERT_EQUAL(2, registered);
    TEST_ASSERT_EQUAL(1, published);
    TEST_ASSERT_EQUAL(2, closed);
    TEST_ASSERT_EQUAL(1, started);
}

void test_legacy_fat32_spelling_uses_host_control_without_a_c_provider(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio3 fat32 /mnt/fat\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_EQUAL(1, started);
}

void test_hosted_mmc_mount_waits_for_the_asynchronous_block_provider(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/mmc0 fat32.wuff /mnt/sd\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(block_device_waited);
    TEST_ASSERT_TRUE(hosted_started);
}

void test_failed_block_readiness_never_initializes_or_publishes_the_hosted_mount(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/mmc0 fat32.wuff /mnt/sd\n";
    fail_block_device_ready = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_TRUE(block_device_waited);
    TEST_ASSERT_FALSE(hosted_started);
    TEST_ASSERT_EQUAL(1, registered);
}

void test_hosted_root_initializes_before_namespace_without_legacy_driver(void)
{
    fstab = "device /dev/virtio4 ext2 /mnt/sd\ndevice /dev/virtio1 ext2.wuff /\n";
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(root);
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_EQUAL(6, registered);
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(2, closed);
}

void test_hosted_root_initialization_failure_never_publishes_or_starts_legacy_driver(void)
{
    fstab = "device /dev/virtio1 ext2.wuff /\n";
    fail_hosted_mount = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_FALSE(root);
    TEST_ASSERT_EQUAL(1, registered);
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(2, closed);
}

void test_hosted_root_publication_failure_has_no_legacy_fallback(void)
{
    fstab = "device /dev/virtio1 ext2.wuff /\n";
    fail_root_publication = true;
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_TRUE(hosted_started);
    TEST_ASSERT_FALSE(root);
    TEST_ASSERT_EQUAL(2, registered);
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(2, closed);
}

void test_duplicate_root_device_across_providers_is_rejected_before_mounting(void)
{
    fstab = "device /dev/virtio1 ext2 /mnt/duplicate\ndevice /dev/virtio1 ext2.wuff /\n";
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, registered);
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
}

void test_duplicate_endpoint_across_providers_is_rejected_before_mounting(void)
{
    fstab = "device /dev/virtio1 ext2.wuff /\ndevice /dev/virtio7 ext2 /\n";
    match_result(
        Hundeschule__mount_filesystems(),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_NOT_EQUAL(IO_OK, code);
        }
    );
    TEST_ASSERT_EQUAL(0, registered);
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
}

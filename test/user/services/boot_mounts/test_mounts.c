#include "unity.h"
#include <string.h>
#include "../../../../user/services/hundeschule/mounts.h"
#include "boot/services.h"
#include "sys/io.h"
#include "sys/service.h"
static const char * fstab;
static unsigned registered;
static unsigned started;
static unsigned closed;
static bool root;
static bool fail_start;
static bool short_reply;
void setUp(void)
{
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio4 fat32 /mnt/sd\n";
    registered = started = closed = 0;
    root = fail_start = short_reply = false;
}
void tearDown(void)
{
}
IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config/fstab"), *path);
    return 7;
}
IOStatus IO__close(IOHandle handle)
{
    return IO_OK;
}
Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos capacity)
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
    return Some(DynString, text);
}
Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr name)
{
    return Success(Channel, ((Channel){0}));
}
ChannelCode Channel__close(ChannelPtr channel)
{
    closed++;
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
            if (BString__equals(&config.mountpoint, &bstr("/")))
            {
                if (root)
                {
                    reply.status = IO_INVALID_MOUNT_POINT;
                }
                root = true;
            }
        },
        error_test_fail()
    );
    memcpy(reply_data, &reply, sizeof(reply));
    registered++;
    return Success(ChannelReplySize, short_reply ? sizeof(reply) - 1 : sizeof(reply));
}
Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0,
                              OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_ASSERT_TRUE(root);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/humpen-wachhund"), *path);
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
    return fail_start ? SERVICE_SCHEDULER_ERROR : SERVICE_OK;
}
Result(ServiceCode)
    BootService__start(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    started++;
    return fail_start ? Error(ServiceCode, bstr("failed"), SERVICE_SCHEDULER_ERROR) : Success(ServiceCode, SERVICE_OK);
}
void test_later_mounts_register_root_and_devices_after_reading_boot_config(void)
{
    match_result(Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(5, registered);
    TEST_ASSERT_EQUAL(4, started);
    TEST_ASSERT_EQUAL(1, closed);
}
void test_missing_root_and_duplicate_root_fail_and_close_control_channel(void)
{
    fstab = "device /dev/virtio4 fat32 /mnt/sd\n";
    match_result(Hundeschule__mount_filesystems(),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
    fstab = "device /dev/virtio1 ext2 /\ndevice /dev/virtio1 ext2 /\n";
    match_result(Hundeschule__mount_filesystems(),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(2, closed);
}
void test_mount_readiness_failure_stops_later_services(void)
{
    fail_start = true;
    match_result(Hundeschule__mount_filesystems(),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_EQUAL(1, started);
    TEST_ASSERT_EQUAL(1, closed);
}
void test_short_mount_reply_is_rejected_before_starting_driver(void)
{
    short_reply = true;
    match_result(Hundeschule__mount_filesystems(),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_EQUAL(0, started);
    TEST_ASSERT_EQUAL(1, closed);
}

#include "unity.h"
#include <string.h>
#define main mama_main
#include "../../../../user/services/hunde-mama/main.c"
#undef main

static uint32_t registers[32 * 0x200 / 4];
static unsigned starts;
static unsigned mounts;
static unsigned fail_start;
static bool school_started;
static bool handed_off;
static bool fail_handoff;
static BString started[9];
static BString mounted[3];
static BootVirtioMmioInfo virtio_mmio;
void setUp(void)
{
    memset(registers, 0, sizeof(registers));
    starts = mounts = fail_start = 0;
    school_started = false;
    handed_off = fail_handoff = false;
    registers[31 * 0x200 / 4] = 0x74726976;
    registers[31 * 0x200 / 4 + 1] = 1;
    registers[31 * 0x200 / 4 + 2] = 2;
    registers[31 * 0x200 / 4 + 0x100 / 4] = 16384;
    virtio_mmio = BootVirtioMmioInfo__new(0x0a000000ULL, 0x200, 32, 48);
}
void tearDown(void)
{
}
ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}
ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}
ServiceCode service_map_mmio(uint64_t physical, size_t size, ServiceMappedMemoryPtr * target)
{
    TEST_ASSERT_EQUAL_HEX64(virtio_mmio.physical_base, physical);
    TEST_ASSERT_EQUAL_UINT64(virtio_mmio.slot_size * virtio_mmio.slot_count, size);
    *target = (ServiceMappedMemoryPtr)registers;
    return SERVICE_OK;
}
Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    BootHardwareInfo info = BootHardwareInfo__new();
    info.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    return Success(BootHardwareInfo, info);
}
void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (uint8_t *)memory + offset;
}
Result(ServiceCode)
    BootService__bootstrap(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    TEST_ASSERT_LESS_THAN(9, starts);
    started[starts++] = *ready;
    return starts == fail_start ? Error(ServiceCode, bstr("failed"), SERVICE_SCHEDULER_ERROR)
                                : Success(ServiceCode, SERVICE_OK);
}
Result(IOStatus) MountClient__add(ChannelPtr self, MountConfig config)
{
    TEST_ASSERT_LESS_THAN(3, mounts);
    mounted[mounts++] = config.mountpoint;
    return Success(IOStatus, IO_OK);
}
Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr name)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), *name);
    return Success(Channel, ((Channel){0}));
}
ChannelCode Channel__close(ChannelPtr channel)
{
    return CHANNEL_OK;
}
Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) first,
                              OptionalPtr(BString) second, OptionalPtr(BString) third)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/hundeschule"), *path);
    TEST_ASSERT_EQUAL(3, mounts);
    TEST_ASSERT_TRUE(handed_off);
    school_started = true;
    return Success(SpawnResult, 99);
}
ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    return PROCESS_CHILD_EVENT_QUEUE_EMPTY;
}

Result(SpawnResult) OS__boot_complete(void)
{
    TEST_ASSERT_EQUAL(3, mounts);
    TEST_ASSERT_FALSE(school_started);
    if (fail_handoff)
    {
        return Error(SpawnResult, bstr("failed"), SPAWN_FAILED);
    }
    handed_off = true;
    return Success(SpawnResult, SPAWN_SUCCESS);
}

void test_mama_does_not_start_school_when_handoff_fails(void)
{
    fail_handoff = true;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(handed_off);
}
SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

void test_mama_establishes_boot_mounts_before_starting_school(void)
{
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_EQUAL(9, starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hundekanal-warter"), started[0]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hunde-puffer"), started[1]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hund-unterbrict"), started[2]);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_FILESYSTEM_SERVICE), started[3]);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_SYSTEM_SERVICE), started[4]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/virtio1"), started[7]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin"), mounted[0]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot"), mounted[1]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config"), mounted[2]);
}
void test_mama_does_not_start_school_when_a_boot_service_fails(void)
{
    fail_start = 9;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_EQUAL(2, mounts);
}
void test_mama_rejects_missing_or_ambiguous_boot_disk(void)
{
    registers[31 * 0x200 / 4 + 0x100 / 4] = 0;
    match_result(MamaBootDisk__find(&virtio_mmio),
        success_test_fail(),
        error_test_ok()
    );
    registers[31 * 0x200 / 4 + 0x100 / 4] = 16384;
    memcpy(registers, registers + 31 * 0x200 / 4, 0x200);
    match_result(MamaBootDisk__find(&virtio_mmio),
        success_test_fail(),
        error_test_ok()
    );
}

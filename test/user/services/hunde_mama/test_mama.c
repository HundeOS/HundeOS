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
static bool disk_ready_waited;
static bool use_sd_host;
static SdHostKind sd_host_kind;
static InterruptControllerKind interrupt_controller_kind;
static unsigned hosted_starts;
static bool fail_hosted_start;
static bool fail_config_publication;
static bool fail_disk_ready;
static bool boot_ready_waited;
static bool startup_banner_reported;
static bool fail_boot_ready;
static BString started[12];
static BString mounted[3];
static BootVirtioMmioInfo virtio_mmio;

void setUp(void)
{
    memset(registers, 0, sizeof(registers));
    starts = mounts = fail_start = 0;
    school_started = false;
    handed_off = fail_handoff = false;
    disk_ready_waited = false;
    use_sd_host = false;
    sd_host_kind = SD_HOST_KIND_CV181X;
    interrupt_controller_kind = INTERRUPT_CONTROLLER_PLIC;
    hosted_starts = 0;
    fail_hosted_start = fail_config_publication = fail_disk_ready = false;
    boot_ready_waited = fail_boot_ready = false;
    startup_banner_reported = false;
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

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    if (BString__equals(name, &bstr(IO_HOST_MOUNT_SERVICE)))
    {
        TEST_ASSERT_EQUAL_UINT(4, starts);
        TEST_ASSERT_EQUAL_UINT(0, mounts);
        boot_ready_waited = true;
        return fail_boot_ready ? SERVICE_SCHEDULER_ERROR : SERVICE_OK;
    }
    TEST_ASSERT_EQUAL_BSTRING(use_sd_host ? bstr("/dev/mmc0p2") : bstr("/dev/virtio1"), *name);
    disk_ready_waited = true;
    return fail_disk_ready ? SERVICE_SCHEDULER_ERROR : SERVICE_OK;
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
    info.interrupt_controller.kind = interrupt_controller_kind;
    if (use_sd_host)
    {
        BootDeviceInfo device = BootDeviceInfo__new(0x04310000, 0x1000, 36);
        info.sd_host = Some(BootSdHostInfo, BootSdHostInfo__new(&device, sd_host_kind, 375000000));
    }
    else
    {
        info.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    }
    return Success(BootHardwareInfo, info);
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (uint8_t *)memory + offset;
}

Result(ServiceCode)
    BootService__bootstrap(ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, ConstBStringPtr ready)
{
    TEST_ASSERT_LESS_THAN(12, starts);
    TEST_ASSERT_FALSE(BString__equals(path, &bstr("/sbin/drivers/ext2fs")));
    TEST_ASSERT_FALSE(BString__equals(path, &bstr("/sbin/drivers/sysinfofs")));
    if (BString__equals(ready, &bstr(IO_SYSTEM_SERVICE)))
    {
        TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/hunde-io-host"), *path);
    }
    started[starts++] = *ready;
    return starts == fail_start ? Error(ServiceCode, bstr("failed"), SERVICE_SCHEDULER_ERROR)
                                : Success(ServiceCode, SERVICE_OK);
}

Result(IOStatus) MountClient__add(ChannelPtr self, MountConfig config)
{
    TEST_ASSERT_LESS_THAN(3, mounts);
    if (BString__equals(&config.mountpoint, &bstr("/boot/config")))
    {
        TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
        TEST_ASSERT_FALSE(fail_hosted_start);
        TEST_ASSERT_EQUAL_BSTRING(bstr("boot-config"), config.filesystem_type);
        if (fail_config_publication)
        {
            return Error(IOStatus, bstr("Mount publication failed"), IO_INVALID_MOUNT_POINT);
        }
    }
    mounted[mounts++] = config.mountpoint;
    return Success(IOStatus, IO_OK);
}

Result(IOStatus) MountClient__start_hosted(MountConfig config)
{
    TEST_ASSERT_TRUE(disk_ready_waited);
    TEST_ASSERT_FALSE(fail_disk_ready);
    TEST_ASSERT_EQUAL_UINT(0, mounts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config"), config.mountpoint);
    TEST_ASSERT_EQUAL_BSTRING(bstr("boot-config"), config.filesystem_type);
    TEST_ASSERT_EQUAL_BSTRING(use_sd_host ? bstr("/dev/mmc0p2") : bstr("/dev/virtio1"), config.device_path);
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(handed_off);
    hosted_starts++;
    if (fail_hosted_start)
    {
        return Error(IOStatus, bstr("Hosted mount failed"), IO_INVALID_PATH);
    }
    return Success(IOStatus, IO_OK);
}

Result(Channel) Channel__open(ServiceDirectoryPtr directory, ConstBStringPtr name)
{
    TEST_ASSERT_TRUE(BString__equals(name, &bstr(IO_FILESYSTEM_SERVICE)) || BString__equals(name, &bstr("/sbin")));
    return Success(Channel, ((Channel){.server_pid = 77}));
}

ChannelCode Channel__close(ChannelPtr channel)
{
    return CHANNEL_OK;
}

Result(SpawnResult) OS__spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) first,
                              OptionalPtr(BString) second, OptionalPtr(BString) third)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/services/hundeschule"), *path);
    TEST_ASSERT_EQUAL(1, mounts);
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
    TEST_ASSERT_EQUAL(1, mounts);
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
    if (strcmp(tag, "hunde-mama") == 0 && strcmp(format, "🐩 HundeOS: hunde-mama begins 🐶") == 0)
    {
        startup_banner_reported = true;
    }
    return SYSLOG_OK;
}

void test_mama_establishes_boot_mounts_before_starting_school(void)
{
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_TRUE(startup_banner_reported);
    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_TRUE(disk_ready_waited);
    TEST_ASSERT_EQUAL(4, starts);
    TEST_ASSERT_TRUE(boot_ready_waited);
    TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hundekanal-warter"), started[0]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hunde-puffer"), started[1]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hund-unterbrict"), started[2]);
    TEST_ASSERT_EQUAL_BSTRING(bstr(IO_SYSTEM_SERVICE), started[3]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/config"), mounted[0]);
}

void test_mama_does_not_start_school_when_a_boot_service_fails(void)
{
    fail_start = 4;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_EQUAL(0, mounts);
    TEST_ASSERT_EQUAL_UINT(0, hosted_starts);
}

void test_mama_uses_hosted_cv181x_on_plic(void)
{
    use_sd_host = true;

    TEST_ASSERT_EQUAL(1, mama_main());

    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_TRUE(disk_ready_waited);
    TEST_ASSERT_EQUAL(6, starts);
    TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), started[4]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p3"), started[5]);
}

void test_mama_bootstraps_partitions_without_restarting_hosted_sd_device(void)
{
    use_sd_host = true;
    sd_host_kind = SD_HOST_KIND_BCM2835;

    TEST_ASSERT_EQUAL(1, mama_main());

    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_TRUE(disk_ready_waited);
    TEST_ASSERT_EQUAL(6, starts);
    TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), started[4]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p3"), started[5]);
}

void test_mama_uses_hosted_pci_sd_device(void)
{
    use_sd_host = true;
    sd_host_kind = SD_HOST_KIND_PCI;

    TEST_ASSERT_EQUAL(1, mama_main());

    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_TRUE(disk_ready_waited);
    TEST_ASSERT_EQUAL(6, starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), started[4]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p3"), started[5]);
}

void test_mama_rejects_unsupported_sd_device_without_starting_a_driver(void)
{
    use_sd_host = true;
    sd_host_kind = SD_HOST_KIND_GENERIC;

    TEST_ASSERT_EQUAL(1, mama_main());

    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(disk_ready_waited);
    TEST_ASSERT_EQUAL(4, starts);
    TEST_ASSERT_EQUAL_UINT(0, hosted_starts);
}

void test_mama_uses_hosted_cv181x_on_gic(void)
{
    use_sd_host = true;
    interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V2;

    TEST_ASSERT_EQUAL(1, mama_main());

    TEST_ASSERT_TRUE(school_started);
    TEST_ASSERT_EQUAL(6, starts);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p2"), started[4]);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/mmc0p3"), started[5]);
}

void test_mama_does_not_publish_or_handoff_when_hosted_boot_mount_fails(void)
{
    fail_hosted_start = true;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
    TEST_ASSERT_EQUAL_UINT(0, mounts);
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(handed_off);
}

void test_mama_does_not_handoff_when_boot_mount_publication_fails(void)
{
    fail_config_publication = true;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_EQUAL_UINT(1, hosted_starts);
    TEST_ASSERT_EQUAL_UINT(0, mounts);
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(handed_off);
}

void test_mama_waits_for_boot_device_before_requesting_hosted_mount(void)
{
    fail_disk_ready = true;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_EQUAL_UINT(0, hosted_starts);
    TEST_ASSERT_EQUAL_UINT(0, mounts);
    TEST_ASSERT_FALSE(school_started);
    TEST_ASSERT_FALSE(handed_off);
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

void test_mama_stops_before_boot_mount_publication_if_initial_filesystems_are_unavailable(void)
{
    fail_boot_ready = true;
    TEST_ASSERT_EQUAL(1, mama_main());
    TEST_ASSERT_TRUE(boot_ready_waited);
    TEST_ASSERT_EQUAL_UINT(0, mounts);
    TEST_ASSERT_FALSE(disk_ready_waited);
    TEST_ASSERT_FALSE(handed_off);
    TEST_ASSERT_FALSE(school_started);
}

#include "unity.h"
#include "../../../arch/aarch64/boot/uefi_devices.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_qemu_uefi_exposes_virtio_boot_devices_without_pi_hardware(void)
{
    BootPlatformInfo platform = {0};
    UefiDevices__configure(&platform);
    match_optional(
        platform.virtio_mmio,
        some(virtio)
        {
            TEST_ASSERT_EQUAL_HEX64(0x0a000000, virtio.physical_base);
            TEST_ASSERT_EQUAL_UINT64(0x200, virtio.slot_size);
            TEST_ASSERT_EQUAL_UINT64(32, virtio.slot_count);
            TEST_ASSERT_EQUAL_UINT64(48, virtio.first_irq);
        },
        none()
        {
            TEST_FAIL_MESSAGE("QEMU UEFI must expose its VirtIO MMIO bus");
        }
    );
    TEST_ASSERT_FALSE(is_something(platform.sd_host));
    TEST_ASSERT_FALSE(is_something(platform.ethernet_mac));
}

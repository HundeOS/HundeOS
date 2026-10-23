#include "unity.h"
#include "../../../arch/aarch64/boot/uefi_devices.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_pi_uefi_preserves_sd_and_ethernet_without_qemu_virtio(void)
{
    BootPlatformInfo platform = {0};
    UefiDevices__configure(&platform);
    TEST_ASSERT_FALSE(is_something(platform.virtio_mmio));
    match_optional(
        platform.sd_host,
        some(sd_host)
        {
            TEST_ASSERT_EQUAL_HEX64(0xfe340000, sd_host.physical_base);
            TEST_ASSERT_EQUAL_UINT64(126, sd_host.irq);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Pi UEFI must retain its SD host");
        }
    );
    match_optional(
        platform.ethernet_mac,
        some(ethernet)
        {
            TEST_ASSERT_EQUAL_HEX64(0xfd580000, ethernet.physical_base);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Pi UEFI must retain its Ethernet controller");
        }
    );
}

#include "unity.h"
#include "boot_info.h"
void setUp(void)
{
}
void tearDown(void)
{
}

void test_boot_info_copies_hardware_without_kernel_virtual_addresses(void)
{
    SystemInfo system = SystemInfo__new();
    system.cpu_count = 4;
    system.wait_mode = BOOT_WAIT_WFE;
    system.uart.physical_base = 0x9000000;
    system.uart.virtual_base = 0xffff000009000000;
    system.uart.irq = 33;
    system.framebuffer =
        Some(FramebufferInfo,
             FramebufferInfo__new(0x30000000, 8192, 64, 32, 64, FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR,
                                  255, 65280, 16711680, 0));
    system.pci = Some(PciInfo, PciInfo__new(0x40000000, 0x100000, 0, 7));
    system.sd_host = Some(SdHostInfo, SdHostInfo__new(0x50000000, 4096, 60));
    BootHardwareInfo result = BootHardwareInfo__from_system(&system, 3);
    TEST_ASSERT_EQUAL_UINT64(4, result.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(3, result.online_cpu_count);
    TEST_ASSERT_EQUAL(BOOT_WAIT_WFE, result.wait_mode);
    TEST_ASSERT_EQUAL_HEX64(0x9000000, result.uart.physical_base);
    TEST_ASSERT_EQUAL_UINT64(33, result.uart.irq);
    match_optional(
        result.framebuffer,
        some(value)
        {
            TEST_ASSERT_EQUAL_UINT64(64, value.width);
            TEST_ASSERT_EQUAL_UINT64(8192, value.size);
        },
        none_test_fail()
    );
    match_optional(result.pci,
        some(value)
        {
            TEST_ASSERT_EQUAL_UINT64(7, value.bus_end);
        },
        none_test_fail()
    );
    match_optional(result.sd_host,
        some(value)
        {
            TEST_ASSERT_EQUAL_UINT64(60, value.irq);
        },
        none_test_fail()
    );
    match_optional(result.ethernet,
        some_test_fail(),
        none_do_nothing()
    );
}

void test_boot_info_preserves_absent_devices(void)
{
    SystemInfo system = SystemInfo__new();
    BootHardwareInfo result = BootHardwareInfo__from_system(&system, 1);
    match_optional(result.framebuffer,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(result.pci,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(result.sd_host,
        some_test_fail(),
        none_do_nothing()
    );
}

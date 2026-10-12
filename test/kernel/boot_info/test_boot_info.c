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
    system.timer_frequency = 10000000;
    system.default_user_stack_size = 2 * 1024 * 1024;
    system.wait_mode = BOOT_WAIT_WFE;
    system.trace_uart = UartInfo__new(0x9000000, 0xffff000009000000, 4096, 33, UART_KIND_PL011, 0, 4);
    system.console_uart = UartInfo__new(0x4150000, 0xffff000004150000, 4096, 45, UART_KIND_NS16550, 2, 4);
    BootVirtioMmioInfo virtio_mmio = BootVirtioMmioInfo__new(0x0a000000, 0x200, 32, 48);
    system.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    system.framebuffer =
        Some(FramebufferInfo,
             FramebufferInfo__new(0x30000000, 8192, 64, 32, 64, FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR,
                                  255, 65280, 16711680, 0));
    system.pci = Some(PciInfo, PciInfo__new(0x40000000, 0x100000, 0, 7));
    system.sd_host =
        Some(SdHostInfo, SdHostInfo__new(0x50000000, 4096, 60, SD_HOST_KIND_CV181X, 375000000));
    BootDeviceInfo rtc_device = BootDeviceInfo__new(0x9010000, 4096, 34);
    system.rtc = Some(BootRtcInfo, BootRtcInfo__new(&rtc_device, RTC_KIND_PL031));
    GicInterruptControllerInfo gic =
        GicInterruptControllerInfo__new(0x8000000, 0xffff000008000000, 0x80a0000, 0xffff0000080a0000, 0, 0);
    system.interrupt_controller = InterruptControllerInfo__new_gic(INTERRUPT_CONTROLLER_GIC_V3, &gic);
    BootHardwareInfo result = BootHardwareInfo__from_system(&system, 3);
    TEST_ASSERT_EQUAL_UINT64(4, result.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(3, result.online_cpu_count);
    TEST_ASSERT_EQUAL_UINT64(10000000, result.timer_frequency);
    TEST_ASSERT_EQUAL_UINT64(2 * 1024 * 1024, result.default_user_stack_size);
    TEST_ASSERT_EQUAL(BOOT_WAIT_WFE, result.wait_mode);
    TEST_ASSERT_EQUAL_HEX64(0x4150000, result.uart.physical_base);
    TEST_ASSERT_EQUAL_UINT64(45, result.uart.irq);
    TEST_ASSERT_EQUAL(UART_KIND_NS16550, result.uart.kind);
    TEST_ASSERT_EQUAL_UINT64(4096, result.uart.mmio_size);
    TEST_ASSERT_EQUAL_UINT64(2, result.uart.register_shift);
    TEST_ASSERT_EQUAL_UINT64(4, result.uart.register_width);
    match_optional(
        result.virtio_mmio,
        some(value)
        {
            TEST_ASSERT_EQUAL_HEX64(0x0a000000, value.physical_base);
            TEST_ASSERT_EQUAL_UINT64(0x200, value.slot_size);
            TEST_ASSERT_EQUAL_UINT64(32, value.slot_count);
            TEST_ASSERT_EQUAL_UINT64(48, value.first_irq);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(INTERRUPT_CONTROLLER_GIC_V3, result.interrupt_controller.kind);
    TEST_ASSERT_EQUAL_HEX64(0x8000000, result.interrupt_controller.details.gic.distributor_physical_base);
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
            TEST_ASSERT_EQUAL_UINT64(60, value.device.irq);
            TEST_ASSERT_EQUAL(SD_HOST_KIND_CV181X, value.kind);
            TEST_ASSERT_EQUAL_UINT64(375000000, value.source_clock_hz);
        },
        none_test_fail()
    );
    match_optional(result.ethernet,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        result.rtc,
        some(value)
        {
            TEST_ASSERT_EQUAL(RTC_KIND_PL031, value.kind);
            TEST_ASSERT_EQUAL_HEX64(0x9010000, value.device.physical_base);
            TEST_ASSERT_EQUAL_UINT64(34, value.device.irq);
        },
        none_test_fail()
    );
}

void test_boot_info_exports_the_default_user_stack_size(void)
{
    SystemInfo system = {};

    BootHardwareInfo result = BootHardwareInfo__from_system(&system, 1);

    TEST_ASSERT_EQUAL_UINT64(SystemInfo__default_user_stack_size(), result.default_user_stack_size);
}

void test_boot_info_exports_plic_without_kernel_virtual_addresses(void)
{
    SystemInfo system = SystemInfo__new();
    PlicInterruptControllerInfo plic = PlicInterruptControllerInfo__new(0xc000000, 0xffff00000c000000, 95, 3);
    system.interrupt_controller = InterruptControllerInfo__new_plic(&plic);

    BootHardwareInfo result = BootHardwareInfo__from_system(&system, 1);

    TEST_ASSERT_EQUAL(INTERRUPT_CONTROLLER_PLIC, result.interrupt_controller.kind);
    TEST_ASSERT_EQUAL_HEX64(0xc000000, result.interrupt_controller.details.plic.physical_base);
    TEST_ASSERT_EQUAL_UINT64(95, result.interrupt_controller.details.plic.source_count);
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
    match_optional(result.virtio_mmio,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(result.rtc,
        some_test_fail(),
        none_do_nothing()
    );
}

#include "unity.h"
#include "hunde/boot_info.h"
#include "sys/boot_info.h"

static BootHardwareInfo hardware;
static bool fail_load;

void setUp(void)
{
    hardware = BootHardwareInfo__new();
    fail_load = false;
}

void tearDown(void)
{
}

Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    return fail_load ? Error(BootHardwareInfo, bstr("Boot info unavailable"), -77)
                     : Success(BootHardwareInfo, hardware);
}

void test_boot_bridge_preserves_scalars_and_absent_peripherals(void)
{
    hardware.cpu_count = 4;
    hardware.online_cpu_count = 3;
    hardware.uart = BootUartInfo__new(0x9000000, 4096, 33, UART_KIND_PL011, 1, 4);
    hardware.memory_start = 0x40000000;
    hardware.memory_end = 0x80000000;
    RustBootInfoResult result = hunde_rust_boot_info();
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.status.kind);
    TEST_ASSERT_EQUAL_UINT64(4, result.info.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(3, result.info.online_cpu_count);
    TEST_ASSERT_EQUAL_UINT64(33, result.info.uart.irq);
    TEST_ASSERT_EQUAL_UINT64(UART_KIND_PL011, result.info.uart.kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.uart.register_shift);
    TEST_ASSERT_EQUAL_UINT64(4, result.info.uart.register_width);
    TEST_ASSERT_EQUAL_UINT64(0x40000000, result.info.memory_start);
    TEST_ASSERT_EQUAL_UINT64(0x80000000, result.info.memory_end);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.rtc_present);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.framebuffer_present);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.pci_present);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.sd_host_present);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.ethernet_present);
}

void test_boot_bridge_preserves_all_optional_records_and_gic(void)
{
    BootDeviceInfo device = BootDeviceInfo__new(0x4310000, 4096, 36);
    hardware.rtc = Some(BootRtcInfo, BootRtcInfo__new(&device, RTC_KIND_GOLDFISH));
    hardware.sd_host = Some(BootSdHostInfo, BootSdHostInfo__new(&device, SD_HOST_KIND_CV181X, 375000000));
    hardware.ethernet = Some(BootEthernetInfo, BootEthernetInfo__new(&device, 7));
    hardware.pci = Some(BootPciInfo, BootPciInfo__new(0x30000000, 0x10000000, 0, 255));
    hardware.framebuffer =
        Some(BootFramebufferInfo, BootFramebufferInfo__new(0x80000000, 8192, 64, 32, 64, 1, 2, 3, 4, 5));
    BootGicInfo gic = BootGicInfo__new(0x8000000, 0x80a0000, 0);
    hardware.interrupt_controller = BootInterruptControllerInfo__new_gic(INTERRUPT_CONTROLLER_GIC_V3, &gic);
    RustBootInfoResult result = hunde_rust_boot_info();
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.status.kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.rtc_present);
    TEST_ASSERT_EQUAL_UINT64(RTC_KIND_GOLDFISH, result.info.rtc.kind);
    TEST_ASSERT_EQUAL_UINT64(36, result.info.rtc.device.irq);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.sd_host_present);
    TEST_ASSERT_EQUAL_UINT64(375000000, result.info.sd_host.source_clock_hz);
    TEST_ASSERT_EQUAL_UINT64(SD_HOST_KIND_CV181X, result.info.sd_host.kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.ethernet_present);
    TEST_ASSERT_EQUAL_UINT64(7, result.info.ethernet.kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.pci_present);
    TEST_ASSERT_EQUAL_UINT64(255, result.info.pci.bus_end);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.framebuffer_present);
    TEST_ASSERT_EQUAL_UINT64(8192, result.info.framebuffer.size);
    TEST_ASSERT_EQUAL_UINT64(5, result.info.framebuffer.reserved_mask);
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_CONTROLLER_GIC_V3, result.info.controller_kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.gic_present);
    TEST_ASSERT_EQUAL_UINT64(0x80a0000, result.info.gic.redistributor_physical_base);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.plic_present);
}

void test_boot_bridge_selects_plic_and_propagates_load_failure(void)
{
    BootPlicInfo plic = BootPlicInfo__new(0xc000000, 95);
    hardware.interrupt_controller = BootInterruptControllerInfo__new_plic(&plic);
    RustBootInfoResult result = hunde_rust_boot_info();
    TEST_ASSERT_EQUAL_UINT64(INTERRUPT_CONTROLLER_PLIC, result.info.controller_kind);
    TEST_ASSERT_EQUAL_UINT64(1, result.info.plic_present);
    TEST_ASSERT_EQUAL_UINT64(95, result.info.plic.source_count);
    TEST_ASSERT_EQUAL_UINT64(0, result.info.gic_present);
    fail_load = true;
    result = hunde_rust_boot_info();
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.status.kind);
    TEST_ASSERT_EQUAL_INT32(-77, result.status.error.code);
}

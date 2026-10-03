#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../arch/riscv64/boot/qemu_virt/device_tree.h"
#include "../../../arch/riscv64/boot/qemu_virt/platform.h"
#include "../../../arch/riscv64/boot/qemu_virt/system_info.h"

#define FDT_MAGIC 0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_END 9U

typedef struct FdtFixture
{
    uint8_t blob[4096];
    uint8_t structure[3072];
    char strings[512];
    uint32_t structure_size;
    uint32_t strings_size;
    PhysicalMemoryRegions reservations;
} FdtFixture;

typedef FdtFixture * FdtFixturePtr;

static FdtFixture FdtFixture__new(void)
{
    return (FdtFixture){
        .reservations = PhysicalMemoryRegions__new(),
    };
}

static void FdtFixture__write_u32(uint8_t * target, uint32_t value)
{
    target[0] = (uint8_t)(value >> 24);
    target[1] = (uint8_t)(value >> 16);
    target[2] = (uint8_t)(value >> 8);
    target[3] = (uint8_t)value;
}

static void FdtFixture__write_u64(uint8_t * target, uint64_t value)
{
    FdtFixture__write_u32(target, (uint32_t)(value >> 32));
    FdtFixture__write_u32(target + sizeof(uint32_t), (uint32_t)value);
}

static void FdtFixture__append_u32(FdtFixturePtr self, uint32_t value)
{
    FdtFixture__write_u32(self->structure + self->structure_size, value);
    self->structure_size += sizeof(uint32_t);
}

static uint32_t FdtFixture__string(FdtFixturePtr self, const char * value)
{
    uint32_t offset = self->strings_size;
    size_t length = strlen(value) + 1U;
    memcpy(self->strings + self->strings_size, value, length);
    self->strings_size += (uint32_t)length;
    return offset;
}

static void FdtFixture__begin_node(FdtFixturePtr self, const char * name)
{
    FdtFixture__append_u32(self, FDT_BEGIN_NODE);
    size_t length = strlen(name) + 1U;
    memcpy(self->structure + self->structure_size, name, length);
    self->structure_size += (uint32_t)length;
    while ((self->structure_size & 3U) != 0)
    {
        self->structure[self->structure_size++] = 0;
    }
}

static void FdtFixture__end_node(FdtFixturePtr self)
{
    FdtFixture__append_u32(self, FDT_END_NODE);
}

static void FdtFixture__property(FdtFixturePtr self, const char * name, const uint8_t * value, uint32_t length)
{
    FdtFixture__append_u32(self, FDT_PROP);
    FdtFixture__append_u32(self, length);
    FdtFixture__append_u32(self, FdtFixture__string(self, name));
    memcpy(self->structure + self->structure_size, value, length);
    self->structure_size += length;
    while ((self->structure_size & 3U) != 0)
    {
        self->structure[self->structure_size++] = 0;
    }
}

static void FdtFixture__property_u32(FdtFixturePtr self, const char * name, uint32_t value)
{
    uint8_t encoded[sizeof(uint32_t)];
    FdtFixture__write_u32(encoded, value);
    FdtFixture__property(self, name, encoded, sizeof(encoded));
}

static void FdtFixture__property_u32_pair(FdtFixturePtr self, const char * name, uint32_t first, uint32_t second)
{
    uint8_t encoded[2U * sizeof(uint32_t)];
    FdtFixture__write_u32(encoded, first);
    FdtFixture__write_u32(encoded + sizeof(uint32_t), second);
    FdtFixture__property(self, name, encoded, sizeof(encoded));
}

static void FdtFixture__property_reg64(FdtFixturePtr self, uint64_t base, uint64_t size)
{
    uint8_t encoded[4U * sizeof(uint32_t)];
    FdtFixture__write_u32(encoded, (uint32_t)(base >> 32));
    FdtFixture__write_u32(encoded + sizeof(uint32_t), (uint32_t)base);
    FdtFixture__write_u32(encoded + (2U * sizeof(uint32_t)), (uint32_t)(size >> 32));
    FdtFixture__write_u32(encoded + (3U * sizeof(uint32_t)), (uint32_t)size);
    FdtFixture__property(self, "reg", encoded, sizeof(encoded));
}

static void FdtFixture__property_string(FdtFixturePtr self, const char * name, const char * value)
{
    FdtFixture__property(self, name, (const uint8_t *)value, (uint32_t)strlen(value) + 1U);
}

static void FdtFixture__reserve(FdtFixturePtr self, uint64_t base, uint64_t size)
{
    PhysicalMemoryRegion reservation = PhysicalMemoryRegion__new(base, base + size);
    match_result(PhysicalMemoryRegions__push(&self->reservations, reservation),
        success_do_nothing(),
                 error_test_fail()
    );
}

static uintptr_t FdtFixture__finish(FdtFixturePtr self)
{
    FdtFixture__append_u32(self, FDT_END);
    uint32_t reservation_offset = 40;
    uint32_t reservation_position = reservation_offset;
    for_each_fa_ptr(
        PhysicalMemoryRegions,
        &self->reservations,
        index,
        reservation,
                    FdtFixture__write_u64(self->blob + reservation_position, reservation.physical_start);
                FdtFixture__write_u64(self->blob + reservation_position + sizeof(uint64_t),
                                      reservation.physical_end - reservation.physical_start);
                reservation_position += 2U * sizeof(uint64_t);
    )
    ;
    FdtFixture__write_u64(self->blob + reservation_position, 0);
    FdtFixture__write_u64(self->blob + reservation_position + sizeof(uint64_t), 0);
    uint32_t structure_offset = reservation_position + (2U * sizeof(uint64_t));
    uint32_t strings_offset = structure_offset + self->structure_size;
    uint32_t total_size = strings_offset + self->strings_size;

    FdtFixture__write_u32(self->blob, FDT_MAGIC);
    FdtFixture__write_u32(self->blob + 4, total_size);
    FdtFixture__write_u32(self->blob + 8, structure_offset);
    FdtFixture__write_u32(self->blob + 12, strings_offset);
    FdtFixture__write_u32(self->blob + 16, reservation_offset);
    FdtFixture__write_u32(self->blob + 20, 17);
    FdtFixture__write_u32(self->blob + 24, 16);
    FdtFixture__write_u32(self->blob + 28, 0);
    FdtFixture__write_u32(self->blob + 32, self->strings_size);
    FdtFixture__write_u32(self->blob + 36, self->structure_size);
    memcpy(self->blob + structure_offset, self->structure, self->structure_size);
    memcpy(self->blob + strings_offset, self->strings, self->strings_size);
    return (uintptr_t)self->blob;
}

static uintptr_t FdtFixture__qemu_virt(FdtFixturePtr self, bool include_plic, bool include_reserved_memory)
{
    FdtFixture__begin_node(self, "");
    FdtFixture__property_u32(self, "#address-cells", 2);
    FdtFixture__property_u32(self, "#size-cells", 2);

    FdtFixture__begin_node(self, "memory@80000000");
    FdtFixture__property_string(self, "device_type", "memory");
    FdtFixture__property_reg64(self, 0x80000000, 0x20000000);
    FdtFixture__end_node(self);

    if (include_reserved_memory)
    {
        FdtFixture__reserve(self, 0x81000000, 0x2000);
        FdtFixture__begin_node(self, "reserved-memory");
        FdtFixture__property_u32(self, "#address-cells", 2);
        FdtFixture__property_u32(self, "#size-cells", 2);
        FdtFixture__begin_node(self, "firmware-data@82000000");
        FdtFixture__property_reg64(self, 0x82000000, 0x3000);
        FdtFixture__end_node(self);
        FdtFixture__end_node(self);
    }

    FdtFixture__begin_node(self, "cpus");
    FdtFixture__property_u32(self, "#address-cells", 1);
    FdtFixture__property_u32(self, "#size-cells", 0);
    FdtFixture__property_u32(self, "timebase-frequency", 10000000);
    FdtFixture__begin_node(self, "cpu@0");
    FdtFixture__property_string(self, "device_type", "cpu");
    FdtFixture__property_u32(self, "reg", 0);
    FdtFixture__end_node(self);
    FdtFixture__end_node(self);

    FdtFixture__begin_node(self, "soc");
    FdtFixture__property_u32(self, "#address-cells", 2);
    FdtFixture__property_u32(self, "#size-cells", 2);

    FdtFixture__begin_node(self, "serial@10000000");
    FdtFixture__property_string(self, "compatible", "ns16550a");
    FdtFixture__property_reg64(self, 0x10000000, 0x100);
    FdtFixture__property_u32(self, "interrupts", 10);
    FdtFixture__end_node(self);

    FdtFixture__begin_node(self, "rtc@101000");
    FdtFixture__property_string(self, "compatible", "google,goldfish-rtc");
    FdtFixture__property_reg64(self, 0x00101000, 0x1000);
    FdtFixture__property_u32(self, "interrupts", 11);
    FdtFixture__end_node(self);

    if (include_plic)
    {
        FdtFixture__begin_node(self, "plic@c000000");
        FdtFixture__property_string(self, "compatible", "sifive,plic-1.0.0");
        FdtFixture__property_reg64(self, 0x0c000000, 0x600000);
        FdtFixture__property_u32(self, "riscv,ndev", 95);
        FdtFixture__end_node(self);
    }

    FdtFixture__begin_node(self, "virtio_mmio@10001000");
    FdtFixture__property_string(self, "compatible", "virtio,mmio");
    FdtFixture__property_reg64(self, 0x10001000, 0x1000);
    FdtFixture__property_u32(self, "interrupts", 1);
    FdtFixture__end_node(self);

    FdtFixture__begin_node(self, "pci@30000000");
    FdtFixture__property_string(self, "compatible", "pci-host-ecam-generic");
    FdtFixture__property_reg64(self, 0x30000000, 0x10000000);
    FdtFixture__property_u32_pair(self, "bus-range", 0, 255);
    FdtFixture__end_node(self);

    FdtFixture__end_node(self);
    FdtFixture__end_node(self);
    return FdtFixture__finish(self);
}

void setUp(void)
{
}

void tearDown(void)
{
}

void test_riscv_device_tree_discovers_qemu_virt_platform(void)
{
    FdtFixture fixture = FdtFixture__new();
    RiscVDeviceTreeResult result = RiscVDeviceTree__parse(FdtFixture__qemu_virt(&fixture, true, false));

    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_OK, result.status);
    TEST_ASSERT_EQUAL_UINT64(0x80000000, result.platform.memory_base);
    TEST_ASSERT_EQUAL_UINT64(0x20000000, result.platform.memory_size);
    TEST_ASSERT_EQUAL_UINT64(1, result.platform.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(10000000, result.platform.timebase_frequency);
    TEST_ASSERT_EQUAL_UINT64(0x10000000, result.platform.uart_base);
    TEST_ASSERT_EQUAL_UINT64(10, result.platform.uart_irq);
    TEST_ASSERT_EQUAL_HEX64(0x00101000, result.platform.rtc_base);
    TEST_ASSERT_EQUAL_UINT64(0x1000, result.platform.rtc_size);
    TEST_ASSERT_EQUAL_UINT64(11, result.platform.rtc_irq);
    TEST_ASSERT_EQUAL_UINT64(0x0c000000, result.platform.plic_base);
    TEST_ASSERT_EQUAL_UINT64(95, result.platform.plic_source_count);
    TEST_ASSERT_EQUAL_UINT64(1, result.platform.virtio_mmio_count);
    TEST_ASSERT_EQUAL_HEX64(0x10001000, result.platform.virtio_mmio_base);
    TEST_ASSERT_EQUAL_UINT64(0x1000, result.platform.virtio_mmio_size);
    TEST_ASSERT_EQUAL_UINT64(1, result.platform.virtio_mmio_first_irq);
    TEST_ASSERT_EQUAL_UINT64(0x30000000, result.platform.pci_ecam_base);
    TEST_ASSERT_EQUAL_UINT64(255, result.platform.pci_bus_end);
}

void test_riscv_device_tree_rejects_invalid_and_truncated_headers(void)
{
    FdtFixture invalid = FdtFixture__new();
    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_INVALID_HEADER, RiscVDeviceTree__parse((uintptr_t)invalid.blob).status);
    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_INVALID_HEADER, RiscVDeviceTree__parse(0).status);

    FdtFixture truncated = FdtFixture__new();
    uintptr_t blob = FdtFixture__qemu_virt(&truncated, true, false);
    FdtFixture__write_u32(truncated.blob + 36, sizeof(truncated.blob));
    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_TRUNCATED, RiscVDeviceTree__parse(blob).status);

    FdtFixture truncated_reservations = FdtFixture__new();
    blob = FdtFixture__qemu_virt(&truncated_reservations, true, false);
    FdtFixture__write_u32(truncated_reservations.blob + 16, sizeof(truncated_reservations.blob));
    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_TRUNCATED, RiscVDeviceTree__parse(blob).status);
}

void test_riscv_device_tree_requires_plic(void)
{
    FdtFixture fixture = FdtFixture__new();
    RiscVDeviceTreeResult result = RiscVDeviceTree__parse(FdtFixture__qemu_virt(&fixture, false, false));

    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_MISSING_PLIC, result.status);
}

void test_riscv_platform_populates_controller_neutral_system_info(void)
{
    FdtFixture fixture = FdtFixture__new();
    RiscVDeviceTreeResult result = RiscVDeviceTree__parse(FdtFixture__qemu_virt(&fixture, true, true));
    RiscVKernelLayout layout = RiscVKernelLayout__new(MemoryRegion__new(0x80200000, 0x80200000, 0x80400000),
                                                      MemoryRegion__new(0xffffffc081000000, 0x81000000, 0x81800000),
                                                      MemoryRegion__new(0x80400000, 0x80400000, 0xa0000000),
                                                      MemoryRegion__new(0x803f0000, 0x803f0000, 0x80400000));

    const uint64_t boot_hart_id = 3;
    SystemInfo info = RiscVPlatformInfo__system_info(&result.platform, &layout, boot_hart_id);

    TEST_ASSERT_EQUAL_UINT64(1, info.cpu_count);
    TEST_ASSERT_EQUAL_UINT64(10000000, info.timer_frequency);
    TEST_ASSERT_EQUAL(BOOT_WAIT_SPIN, info.wait_mode);
    TEST_ASSERT_EQUAL_HEX64(0x80000000, info.physical_memory.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0xa0000000, info.physical_memory.physical_end);
    TEST_ASSERT_EQUAL_HEX64(0xffffffc081000000, info.embedded_binaries.virtual_start);
    TEST_ASSERT_EQUAL_HEX64(0x81000000, info.embedded_binaries.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0x81800000, info.embedded_binaries.physical_end);
    match_optional(
        info.virtio_mmio,
        some(virtio_mmio)
        {
            TEST_ASSERT_EQUAL_HEX64(0x10001000, virtio_mmio.physical_base);
            TEST_ASSERT_EQUAL_UINT64(0x1000, virtio_mmio.slot_size);
            TEST_ASSERT_EQUAL_UINT64(1, virtio_mmio.slot_count);
            TEST_ASSERT_EQUAL_UINT64(1, virtio_mmio.first_irq);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL_HEX64(0x10000000, info.uart.physical_base);
    TEST_ASSERT_EQUAL_HEX64(RISCV_HIGHER_HALF_OFFSET + 0x10000000, info.uart.virtual_base);
    match_optional(
        info.rtc,
        some(rtc)
        {
            TEST_ASSERT_EQUAL_HEX64(0x00101000, rtc.device.physical_base);
            TEST_ASSERT_EQUAL_UINT64(0x1000, rtc.device.mmio_size);
            TEST_ASSERT_EQUAL_UINT64(11, rtc.device.irq);
            TEST_ASSERT_EQUAL(RTC_KIND_GOLDFISH, rtc.kind);
        },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(INTERRUPT_CONTROLLER_PLIC, info.interrupt_controller.kind);
    TEST_ASSERT_EQUAL_HEX64(0x0c000000, info.interrupt_controller.details.plic.physical_base);
    TEST_ASSERT_EQUAL_HEX64(RISCV_HIGHER_HALF_OFFSET + 0x0c000000, info.interrupt_controller.details.plic.virtual_base);
    TEST_ASSERT_EQUAL_UINT64(95, info.interrupt_controller.details.plic.source_count);
    TEST_ASSERT_EQUAL_UINT64(boot_hart_id, info.interrupt_controller.details.plic.target_hart_id);
    TEST_ASSERT_EQUAL_UINT64(3, PhysicalMemoryRegions__len(&info.physical_memory.free_regions));
    PhysicalMemoryRegion first = unwrap_optional(PhysicalMemoryRegions__at(&info.physical_memory.free_regions, 0),
                                                 PhysicalMemoryRegion__new(0, 0));
    PhysicalMemoryRegion second = unwrap_optional(PhysicalMemoryRegions__at(&info.physical_memory.free_regions, 1),
                                                  PhysicalMemoryRegion__new(0, 0));
    PhysicalMemoryRegion third = unwrap_optional(PhysicalMemoryRegions__at(&info.physical_memory.free_regions, 2),
                                                 PhysicalMemoryRegion__new(0, 0));
    TEST_ASSERT_EQUAL_HEX64(0x80400000, first.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0x81000000, first.physical_end);
    TEST_ASSERT_EQUAL_HEX64(0x81002000, second.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0x82000000, second.physical_end);
    TEST_ASSERT_EQUAL_HEX64(0x82003000, third.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0xa0000000, third.physical_end);
    match_optional(
        info.pci,
        some(pci)
        {
            TEST_ASSERT_EQUAL_HEX64(0x30000000, pci.ecam_physical_base);
            TEST_ASSERT_EQUAL_UINT64(255, pci.bus_end);
        },
        none_test_fail()
    );
}

void test_riscv_device_tree_collects_header_and_reserved_memory_nodes(void)
{
    FdtFixture fixture = FdtFixture__new();
    RiscVDeviceTreeResult result = RiscVDeviceTree__parse(FdtFixture__qemu_virt(&fixture, true, true));

    TEST_ASSERT_EQUAL(RISCV_DEVICE_TREE_OK, result.status);
    TEST_ASSERT_EQUAL_UINT64(2, PhysicalMemoryRegions__len(&result.platform.reserved_memory_regions));
    PhysicalMemoryRegion header = unwrap_optional(
        PhysicalMemoryRegions__at(&result.platform.reserved_memory_regions, 0), PhysicalMemoryRegion__new(0, 0));
    PhysicalMemoryRegion node = unwrap_optional(PhysicalMemoryRegions__at(&result.platform.reserved_memory_regions, 1),
                                                PhysicalMemoryRegion__new(0, 0));
    TEST_ASSERT_EQUAL_HEX64(0x81000000, header.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0x81002000, header.physical_end);
    TEST_ASSERT_EQUAL_HEX64(0x82000000, node.physical_start);
    TEST_ASSERT_EQUAL_HEX64(0x82003000, node.physical_end);
}

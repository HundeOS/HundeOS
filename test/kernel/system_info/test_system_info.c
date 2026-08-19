#include "unity.h"
#include <stdint.h>
#include <string.h>
#include "../../../kernel/system_info.h"
#include "../../../arch/aarch64/boot/uefi.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static SystemInfo test_system_info(void)
{
    return (SystemInfo) {
        .kernel_image = {
            .virtual_start = 0xffff000000100000ULL,
            .physical_start = 0x80200000ULL,
            .physical_end = 0x80400000ULL,
        },
        .kernel_heap = {
            .virtual_start = 0xfffff00000000000ULL,
            .physical_start = 0x90000000ULL,
            .physical_end = 0x90400000ULL,
        },
        .kernel_stack = {
            .virtual_start = 0xffff800000000000ULL,
            .physical_start = 0xc0000000ULL,
            .physical_end = 0xc0400000ULL,
        },
        .embedded_binaries = {
            .virtual_start = 0xffffa00000000000ULL,
            .physical_start = 0x88000000ULL,
            .physical_end = 0x88200000ULL,
        },
        .interrupt_controller = {
            .kind = INTERRUPT_CONTROLLER_GIC_V3,
            .distributor_physical_base = 0x08000000ULL,
            .distributor_virtual_base = 0xffff000000400000ULL,
            .redistributor_physical_base = 0x080a0000ULL,
            .redistributor_virtual_base = 0xffff0000004a0000ULL,
            .cpu_interface_physical_base = 0,
            .cpu_interface_virtual_base = 0,
        },
    };
}

void test_system_info_translates_kernel_image_physical_address(void)
{
    SystemInfo info = test_system_info();

    KernelAddress address = SystemInfo__kernel_address_for_physical(&info, 0x80201000ULL);

    TEST_ASSERT_TRUE(address.valid);
    TEST_ASSERT_EQUAL_UINT64(0xffff000000101000ULL, address.virtual_addr);
    TEST_ASSERT_EQUAL_UINT64(0x80201000ULL, address.phys_addr);
}

void test_system_info_translates_heap_virtual_address(void)
{
    SystemInfo info = test_system_info();

    KernelAddress address = SystemInfo__kernel_address_for_virtual(&info, 0xfffff00000002000ULL);

    TEST_ASSERT_TRUE(address.valid);
    TEST_ASSERT_EQUAL_UINT64(0xfffff00000002000ULL, address.virtual_addr);
    TEST_ASSERT_EQUAL_UINT64(0x90002000ULL, address.phys_addr);
}

void test_system_info_returns_invalid_for_unknown_physical_address(void)
{
    SystemInfo info = test_system_info();

    KernelAddress address = SystemInfo__kernel_address_for_physical(&info, 0x70000000ULL);

    TEST_ASSERT_FALSE(address.valid);
}

typedef struct TestEfiTableHeader
{
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
}
TestEfiTableHeader;

typedef struct TestEfiGuid
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
}
TestEfiGuid;

typedef struct TestEfiConfigurationTable
{
    TestEfiGuid vendor_guid;
    uint64_t vendor_table;
}
TestEfiConfigurationTable;

typedef struct TestEfiSystemTable
{
    TestEfiTableHeader header;
    uint64_t firmware_vendor;
    uint32_t firmware_revision;
    uint32_t reserved;
    uint64_t console_in_handle;
    uint64_t console_in;
    uint64_t console_out_handle;
    uint64_t console_out;
    uint64_t standard_error_handle;
    uint64_t standard_error;
    uint64_t runtime_services;
    uint64_t boot_services;
    uint64_t number_of_table_entries;
    TestEfiConfigurationTable * configuration_table;
}
TestEfiSystemTable;

static void write_le32(uint8_t * buffer, size_t offset, uint32_t value)
{
    buffer[offset] = (uint8_t) value;
    buffer[offset + 1] = (uint8_t)(value >> 8);
    buffer[offset + 2] = (uint8_t)(value >> 16);
    buffer[offset + 3] = (uint8_t)(value >> 24);
}

static void write_le64(uint8_t * buffer, size_t offset, uint64_t value)
{
    write_le32(buffer, offset, (uint32_t) value);
    write_le32(buffer, offset + 4, (uint32_t)(value >> 32));
}

static void write_sig(uint8_t * buffer, const char * signature)
{
    buffer[0] = (uint8_t) signature[0];
    buffer[1] = (uint8_t) signature[1];
    buffer[2] = (uint8_t) signature[2];
    buffer[3] = (uint8_t) signature[3];
}

static void set_checksum(uint8_t * buffer, size_t checksum_offset, size_t len)
{
    buffer[checksum_offset] = 0;
    uint8_t sum = 0;
    for (size_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + buffer[i]);
    }
    buffer[checksum_offset] = (uint8_t)(0 - sum);
}

void test_uefi_acpi_populates_uart_and_gic_platform_info(void)
{
    uint8_t rsdp[36] = { 0 };
    uint8_t xsdt[36 + 24] = { 0 };
    uint8_t spcr[64] = { 0 };
    uint8_t madt[44 + 24 + 80 + 16] = { 0 };
    uint8_t mcfg[44 + 16] = { 0 };

    memcpy(rsdp, "RSD PTR ", 8);
    rsdp[15] = 2;
    write_le32(rsdp, 20, sizeof(rsdp));
    write_le64(rsdp, 24, (uint64_t)(uintptr_t) xsdt);
    set_checksum(rsdp, 8, 20);
    set_checksum(rsdp, 32, sizeof(rsdp));

    write_sig(xsdt, "XSDT");
    write_le32(xsdt, 4, sizeof(xsdt));
    write_le64(xsdt, 36, (uint64_t)(uintptr_t) spcr);
    write_le64(xsdt, 44, (uint64_t)(uintptr_t) madt);
    write_le64(xsdt, 52, (uint64_t)(uintptr_t) mcfg);
    set_checksum(xsdt, 9, sizeof(xsdt));

    write_sig(spcr, "SPCR");
    write_le32(spcr, 4, sizeof(spcr));
    write_le64(spcr, 44, 0xfe201000ULL);
    spcr[53] = 57;
    write_le32(spcr, 54, 153);
    set_checksum(spcr, 9, sizeof(spcr));

    write_sig(madt, "APIC");
    write_le32(madt, 4, sizeof(madt));
    size_t gicd = 44;
    madt[gicd] = 0x0c;
    madt[gicd + 1] = 24;
    write_le64(madt, gicd + 8, 0xff841000ULL);
    madt[gicd + 20] = 3;
    size_t gicc = gicd + 24;
    madt[gicc] = 0x0b;
    madt[gicc + 1] = 80;
    write_le64(madt, gicc + 32, 0xff842000ULL);
    size_t gicr = gicc + 80;
    madt[gicr] = 0x0e;
    madt[gicr + 1] = 16;
    write_le64(madt, gicr + 4, 0xff848000ULL);
    set_checksum(madt, 9, sizeof(madt));

    write_sig(mcfg, "MCFG");
    write_le32(mcfg, 4, sizeof(mcfg));
    write_le64(mcfg, 44, 0x4010000000ULL);
    mcfg[54] = 0;
    mcfg[55] = 255;
    set_checksum(mcfg, 9, sizeof(mcfg));

    TestEfiConfigurationTable config = {
        .vendor_guid = {
            .data1 = 0x8868e871,
            .data2 = 0xe4f1,
            .data3 = 0x11d3,
            .data4 = { 0xbc, 0x22, 0x00, 0x80, 0xc7, 0x3c, 0x88, 0x81 },
        },
        .vendor_table = (uint64_t)(uintptr_t) rsdp,
    };
    TestEfiSystemTable system_table = {
        .header = {
            .signature = 0x5453595320494249ULL,
        },
        .number_of_table_entries = 1,
        .configuration_table = &config,
    };
    BootPlatformInfo platform = {
        .uart_physical_base = 0x3f201000ULL,
        .uart_virtual_base = 0xffff000000000000ULL,
        .uart_irq = 99,
        .interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V2,
        .gic_distributor_physical_base = 0x11110000ULL,
        .gic_distributor_virtual_base = 0xffff000000400000ULL,
    };

    UefiAcpi__apply_system_table(&platform, (uint64_t)(uintptr_t) &system_table);

    TEST_ASSERT_EQUAL_UINT64(0xfe201000ULL, platform.uart_physical_base);
    TEST_ASSERT_EQUAL_UINT64(153, platform.uart_irq);
    TEST_ASSERT_EQUAL_INT(INTERRUPT_CONTROLLER_GIC_V3, platform.interrupt_controller_kind);
    TEST_ASSERT_EQUAL_UINT64(0xff841000ULL, platform.gic_distributor_physical_base);
    TEST_ASSERT_EQUAL_UINT64(0xff848000ULL, platform.gic_redistributor_physical_base);
    TEST_ASSERT_EQUAL_UINT64(0xff842000ULL, platform.gic_cpu_interface_physical_base);
    Match(PciInfo, platform.pci)
        IsSome(info,
        {
            TEST_ASSERT_EQUAL_UINT64(0x4010000000ULL, info.ecam_physical_base);
            TEST_ASSERT_EQUAL_UINT64(0x10000000ULL, info.ecam_size);
            TEST_ASSERT_EQUAL_UINT64(0, info.bus_start);
            TEST_ASSERT_EQUAL_UINT64(255, info.bus_end);
        })
        IsNone(TEST_FAIL_MESSAGE("UEFI ACPI MCFG should populate PCI info");)
    End
}

#include "uefi.h"

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#define EFI_SYSTEM_TABLE_SIGNATURE 0x5453595320494249ULL
#define ACPI_20_TABLE_GUID_DATA1 0x8868e871U
#define ACPI_20_TABLE_GUID_DATA2 0xe4f1U
#define ACPI_20_TABLE_GUID_DATA3 0x11d3U
#define ACPI_10_TABLE_GUID_DATA1 0xeb9d2d30U
#define ACPI_10_TABLE_GUID_DATA2 0x2d88U
#define ACPI_10_TABLE_GUID_DATA3 0x11d3U
#define ACPI_TABLE_SCAN_LIMIT 256ULL

#define ACPI_MADT_TYPE_GIC_CPU_INTERFACE 0x0BU
#define ACPI_MADT_TYPE_GIC_DISTRIBUTOR 0x0CU
#define ACPI_MADT_TYPE_GIC_REDISTRIBUTOR 0x0EU
#define ACPI_RSDP_SIGNATURE 0x2052545020445352ULL
#define ACPI_SPCR_SIGNATURE 0x52435053U
#define ACPI_MADT_SIGNATURE 0x43495041U
#define ACPI_MCFG_SIGNATURE 0x4746434dU
#define ACPI_XSDT_SIGNATURE 0x54445358U
#define ACPI_RSDT_SIGNATURE 0x54445352U
#define ACPI_MCFG_ENTRY_SIZE 16U
#define ACPI_PCI_ECAM_BUS_SIZE 0x100000ULL

typedef struct EfiTableHeader
{
    uint64_t signature;
    uint32_t revision;
    uint32_t header_size;
    uint32_t crc32;
    uint32_t reserved;
} EfiTableHeader;

typedef struct EfiGuid
{
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t data4[8];
} EfiGuid;

typedef const EfiGuid * ConstEfiGuidPtr;

typedef struct EfiConfigurationTable
{
    EfiGuid vendor_guid;
    uint64_t vendor_table;
} EfiConfigurationTable;

typedef EfiConfigurationTable * EfiConfigurationTablePtr;

typedef struct EfiSystemTable
{
    EfiTableHeader header;
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
    EfiConfigurationTablePtr configuration_table;
} EfiSystemTable;

typedef EfiSystemTable * EfiSystemTablePtr;

static __BOOT__(uint8_t) UefiAcpi__read8(uint64_t address)
{
    return *(volatile uint8_t *)address;
}

static __BOOT__(uint16_t) UefiAcpi__read16(uint64_t address)
{
    return ((uint16_t)UefiAcpi__read8(address)) | ((uint16_t)UefiAcpi__read8(address + 1) << 8);
}

static __BOOT__(uint32_t) UefiAcpi__read32(uint64_t address)
{
    return ((uint32_t)UefiAcpi__read16(address)) | ((uint32_t)UefiAcpi__read16(address + 2) << 16);
}

static __BOOT__(uint64_t) UefiAcpi__read64(uint64_t address)
{
    return ((uint64_t)UefiAcpi__read32(address)) | ((uint64_t)UefiAcpi__read32(address + 4) << 32);
}

static __BOOT__(bool) UefiAcpi__checksum_ok(uint64_t address, uint32_t len)
{
    uint8_t sum = 0;
    for (uint32_t i = 0; i < len; i++)
    {
        sum = (uint8_t)(sum + UefiAcpi__read8(address + i));
    }

    return sum == 0;
}

static __BOOT__(bool) UefiAcpi__guid_is_acpi20(ConstEfiGuidPtr guid)
{
    return guid->data1 == ACPI_20_TABLE_GUID_DATA1 && guid->data2 == ACPI_20_TABLE_GUID_DATA2 &&
           guid->data3 == ACPI_20_TABLE_GUID_DATA3 && guid->data4[0] == 0xbc && guid->data4[1] == 0x22 &&
           guid->data4[2] == 0x00 && guid->data4[3] == 0x80 && guid->data4[4] == 0xc7 && guid->data4[5] == 0x3c &&
           guid->data4[6] == 0x88 && guid->data4[7] == 0x81;
}

static __BOOT__(bool) UefiAcpi__guid_is_acpi10(ConstEfiGuidPtr guid)
{
    return guid->data1 == ACPI_10_TABLE_GUID_DATA1 && guid->data2 == ACPI_10_TABLE_GUID_DATA2 &&
           guid->data3 == ACPI_10_TABLE_GUID_DATA3 && guid->data4[0] == 0x9a && guid->data4[1] == 0x16 &&
           guid->data4[2] == 0x00 && guid->data4[3] == 0x90 && guid->data4[4] == 0x27 && guid->data4[5] == 0x3f &&
           guid->data4[6] == 0xc1 && guid->data4[7] == 0x4d;
}

static __BOOT__(uint64_t) UefiAcpi__find_rsdp(uint64_t system_table)
{
    if (system_table == 0)
    {
        return 0;
    }

    EfiSystemTablePtr table = (EfiSystemTablePtr)system_table;
    if (table->header.signature != EFI_SYSTEM_TABLE_SIGNATURE || table->configuration_table == 0)
    {
        return 0;
    }

    uint64_t entries = table->number_of_table_entries;
    if (entries > ACPI_TABLE_SCAN_LIMIT)
    {
        entries = ACPI_TABLE_SCAN_LIMIT;
    }

    for (uint64_t i = 0; i < entries; i++)
    {
        EfiConfigurationTablePtr config = &table->configuration_table[i];
        if (UefiAcpi__guid_is_acpi20(&config->vendor_guid) || UefiAcpi__guid_is_acpi10(&config->vendor_guid))
        {
            return config->vendor_table;
        }
    }

    return 0;
}

static __BOOT__(bool) UefiAcpi__sdt_valid(uint64_t table, uint32_t signature)
{
    if (table == 0 || UefiAcpi__read32(table) != signature)
    {
        return false;
    }

    uint32_t len = UefiAcpi__read32(table + 4);
    return len >= 36 && UefiAcpi__checksum_ok(table, len);
}

static __BOOT__(void) UefiAcpi__apply_spcr(BootPlatformInfoPtr platform, uint64_t spcr)
{
    uint64_t uart_base = UefiAcpi__read64(spcr + 44);
    uint8_t irq = UefiAcpi__read8(spcr + 53);
    uint32_t gsi = UefiAcpi__read32(spcr + 54);

    if (uart_base != 0)
    {
        platform->uart_physical_base = uart_base;
    }

    if (gsi != 0)
    {
        platform->uart_irq = gsi;
    }
    else if (irq != 0)
    {
        platform->uart_irq = irq;
    }
}

static __BOOT__(void) UefiAcpi__apply_madt_gicc(BootPlatformInfoPtr platform, uint64_t entry)
{
    uint64_t cpu_interface_base = UefiAcpi__read64(entry + 32);
    uint64_t redistributor_base = UefiAcpi__read64(entry + 60);

    if (cpu_interface_base != 0)
    {
        platform->gic_cpu_interface_physical_base = cpu_interface_base;
        platform->gic_cpu_interface_virtual_base =
            platform->gic_distributor_virtual_base + (cpu_interface_base & ((2ULL * 1024ULL * 1024ULL) - 1ULL));
    }

    if (redistributor_base != 0)
    {
        platform->gic_redistributor_physical_base = redistributor_base;
        platform->interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V3;
    }
}

static __BOOT__(void) UefiAcpi__apply_madt_gicd(BootPlatformInfoPtr platform, uint64_t entry)
{
    uint64_t distributor_base = UefiAcpi__read64(entry + 8);
    uint8_t gic_version = UefiAcpi__read8(entry + 20);

    if (distributor_base != 0)
    {
        platform->gic_distributor_physical_base = distributor_base;
    }

    if (gic_version >= 3)
    {
        platform->interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V3;
    }
    else if (gic_version != 0)
    {
        platform->interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V2;
    }
}

static __BOOT__(void) UefiAcpi__apply_madt_gicr(BootPlatformInfoPtr platform, uint64_t entry)
{
    uint64_t redistributor_base = UefiAcpi__read64(entry + 4);

    if (redistributor_base != 0)
    {
        platform->gic_redistributor_physical_base = redistributor_base;
        platform->interrupt_controller_kind = INTERRUPT_CONTROLLER_GIC_V3;
    }
}

static __BOOT__(void) UefiAcpi__apply_madt(BootPlatformInfoPtr platform, uint64_t madt)
{
    uint32_t len = UefiAcpi__read32(madt + 4);
    uint32_t offset = 44;

    while (offset + 2 <= len)
    {
        uint64_t entry = madt + offset;
        uint8_t type = UefiAcpi__read8(entry);
        uint8_t entry_len = UefiAcpi__read8(entry + 1);

        if (entry_len < 2 || offset + entry_len > len)
        {
            return;
        }

        if (type == ACPI_MADT_TYPE_GIC_CPU_INTERFACE)
        {
            UefiAcpi__apply_madt_gicc(platform, entry);
        }
        else if (type == ACPI_MADT_TYPE_GIC_DISTRIBUTOR)
        {
            UefiAcpi__apply_madt_gicd(platform, entry);
        }
        else if (type == ACPI_MADT_TYPE_GIC_REDISTRIBUTOR)
        {
            UefiAcpi__apply_madt_gicr(platform, entry);
        }

        offset += entry_len;
    }
}

static __BOOT__(void) UefiAcpi__apply_mcfg(BootPlatformInfoPtr platform, uint64_t mcfg)
{
    uint32_t len = UefiAcpi__read32(mcfg + 4);
    uint32_t offset = 44;

    while (offset + ACPI_MCFG_ENTRY_SIZE <= len)
    {
        uint64_t entry = mcfg + offset;
        uint64_t ecam_base = UefiAcpi__read64(entry);
        uint64_t bus_start = UefiAcpi__read8(entry + 10);
        uint64_t bus_end = UefiAcpi__read8(entry + 11);

        if (ecam_base != 0 && bus_end >= bus_start)
        {
            platform->pci =
                Some(PciInfo, PciInfo__new(ecam_base, ((bus_end - bus_start) + 1ULL) * ACPI_PCI_ECAM_BUS_SIZE,
                                           bus_start, bus_end));
            return;
        }

        offset += ACPI_MCFG_ENTRY_SIZE;
    }
}

static __BOOT__(void) UefiAcpi__apply_table(BootPlatformInfoPtr platform, uint64_t table)
{
    if (UefiAcpi__sdt_valid(table, ACPI_SPCR_SIGNATURE))
    {
        UefiAcpi__apply_spcr(platform, table);
    }
    else if (UefiAcpi__sdt_valid(table, ACPI_MADT_SIGNATURE))
    {
        UefiAcpi__apply_madt(platform, table);
    }
    else if (UefiAcpi__sdt_valid(table, ACPI_MCFG_SIGNATURE))
    {
        UefiAcpi__apply_mcfg(platform, table);
    }
}

static __BOOT__(void) UefiAcpi__apply_xsdt(BootPlatformInfoPtr platform, uint64_t xsdt)
{
    if (!UefiAcpi__sdt_valid(xsdt, ACPI_XSDT_SIGNATURE))
    {
        return;
    }

    uint32_t len = UefiAcpi__read32(xsdt + 4);
    uint32_t entries = (len - 36) / 8;
    for (uint32_t i = 0; i < entries; i++)
    {
        UefiAcpi__apply_table(platform, UefiAcpi__read64(xsdt + 36 + ((uint64_t)i * 8ULL)));
    }
}

static __BOOT__(void) UefiAcpi__apply_rsdt(BootPlatformInfoPtr platform, uint64_t rsdt)
{
    if (!UefiAcpi__sdt_valid(rsdt, ACPI_RSDT_SIGNATURE))
    {
        return;
    }

    uint32_t len = UefiAcpi__read32(rsdt + 4);
    uint32_t entries = (len - 36) / 4;
    for (uint32_t i = 0; i < entries; i++)
    {
        UefiAcpi__apply_table(platform, UefiAcpi__read32(rsdt + 36 + ((uint64_t)i * 4ULL)));
    }
}

__BOOT__(void) UefiAcpi__apply_system_table(BootPlatformInfoPtr platform, uint64_t system_table)
{
    uint64_t rsdp = UefiAcpi__find_rsdp(system_table);
    if (rsdp == 0 || UefiAcpi__read64(rsdp) != ACPI_RSDP_SIGNATURE || !UefiAcpi__checksum_ok(rsdp, 20))
    {
        return;
    }

    uint8_t revision = UefiAcpi__read8(rsdp + 15);
    uint64_t xsdt = 0;
    if (revision >= 2)
    {
        uint32_t len = UefiAcpi__read32(rsdp + 20);
        if (len >= 36 && UefiAcpi__checksum_ok(rsdp, len))
        {
            xsdt = UefiAcpi__read64(rsdp + 24);
        }
    }

    if (xsdt != 0)
    {
        UefiAcpi__apply_xsdt(platform, xsdt);
    }
    else
    {
        UefiAcpi__apply_rsdt(platform, UefiAcpi__read32(rsdp + 16));
    }
}

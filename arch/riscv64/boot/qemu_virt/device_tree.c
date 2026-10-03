#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "device_tree.h"

#define FDT_MAGIC 0xd00dfeedU
#define FDT_BEGIN_NODE 1U
#define FDT_END_NODE 2U
#define FDT_PROP 3U
#define FDT_NOP 4U
#define FDT_END 9U
#define FDT_HEADER_SIZE 40U
#define FDT_MAX_DEPTH 16

typedef const uint8_t * ConstFdtBytesPtr;
typedef const char * ConstFdtTextPtr;

typedef struct FdtRegister
{
    bool present;
    uint64_t base;
    uint64_t size;
} FdtRegister;

static FdtRegister FdtRegister__new(void)
{
    return (FdtRegister){};
}

typedef struct FdtNode
{
    uint32_t parent_address_cells;
    uint32_t parent_size_cells;
    uint32_t child_address_cells;
    uint32_t child_size_cells;
    FdtRegister reg;
    bool enabled;
    bool memory;
    bool cpu;
    bool uart;
    bool rtc;
    bool plic;
    bool virtio_mmio;
    bool pci_ecam;
    bool reserved_memory_container;
    bool reserved_memory_child;
    bool has_interrupt;
    uint64_t interrupt;
    bool has_plic_source_count;
    uint64_t plic_source_count;
    bool has_timebase_frequency;
    uint64_t timebase_frequency;
    bool has_bus_range;
    uint64_t bus_start;
    uint64_t bus_end;
} FdtNode;

typedef FdtNode * FdtNodePtr;

static FdtNode FdtNode__new(uint32_t parent_address_cells, uint32_t parent_size_cells)
{
    return (FdtNode){
        .parent_address_cells = parent_address_cells,
        .parent_size_cells = parent_size_cells,
        .child_address_cells = 2,
        .child_size_cells = 1,
        .reg = FdtRegister__new(),
        .enabled = true,
    };
}

static RiscVDeviceTreeResult RiscVDeviceTreeResult__new(RiscVDeviceTreeStatus status,
                                                        ConstRiscVPlatformInfoPtr platform)
{
    return (RiscVDeviceTreeResult){
        .status = status,
        .platform = *platform,
    };
}

static uint32_t Fdt__read_u32(ConstFdtBytesPtr bytes)
{
    return ((uint32_t)bytes[0] << 24) | ((uint32_t)bytes[1] << 16) | ((uint32_t)bytes[2] << 8) | (uint32_t)bytes[3];
}

static uint64_t Fdt__read_u64(ConstFdtBytesPtr bytes)
{
    return ((uint64_t)Fdt__read_u32(bytes) << 32) | Fdt__read_u32(bytes + sizeof(uint32_t));
}

static bool Fdt__range_valid(uint64_t offset, uint64_t size, uint64_t total)
{
    return offset <= total && size <= total - offset;
}

static uint64_t Fdt__align_u32(uint64_t value)
{
    return (value + 3U) & ~3ULL;
}

static bool FdtText__equals(ConstFdtBytesPtr text, uint64_t length, ConstFdtTextPtr expected, uint64_t expected_length)
{
    if (length != expected_length)
    {
        return false;
    }

    for (uint64_t index = 0; index < length; index++)
    {
        if (text[index] != (uint8_t)expected[index])
        {
            return false;
        }
    }
    return true;
}

static bool FdtStringList__contains(ConstFdtBytesPtr value, uint64_t length, ConstFdtTextPtr expected,
                                    uint64_t expected_length)
{
    uint64_t position = 0;
    while (position < length)
    {
        uint64_t item_length = 0;
        while (position + item_length < length && value[position + item_length] != 0)
        {
            item_length++;
        }
        if (position + item_length >= length)
        {
            return false;
        }
        if (FdtText__equals(value + position, item_length, expected, expected_length))
        {
            return true;
        }
        position += item_length + 1U;
    }
    return false;
}

static bool FdtProperty__name_equals(ConstFdtBytesPtr name, uint64_t available, ConstFdtTextPtr expected,
                                     uint64_t expected_length)
{
    if (expected_length >= available || name[expected_length] != 0)
    {
        return false;
    }
    return FdtText__equals(name, expected_length, expected, expected_length);
}

static RiscVDeviceTreeStatus RiscVPlatformInfo__reserve(RiscVPlatformInfoPtr self, uint64_t base, uint64_t size)
{
    if (size == 0)
    {
        return RISCV_DEVICE_TREE_OK;
    }
    if (base > UINT64_MAX - size)
    {
        return RISCV_DEVICE_TREE_MALFORMED;
    }

    PhysicalMemoryRegion region = PhysicalMemoryRegion__new(base, base + size);
    match_result(
        PhysicalMemoryRegions__push(&self->reserved_memory_regions, region),
        success_do_nothing(),
        error(code, message)
        {
            return RISCV_DEVICE_TREE_TOO_MANY_RESERVED_REGIONS;
        }
    );
    return RISCV_DEVICE_TREE_OK;
}

static RiscVDeviceTreeStatus Fdt__read_reservation_map(ConstFdtBytesPtr blob, uint64_t total_size,
                                                       uint64_t reservation_offset, RiscVPlatformInfoPtr platform)
{
    uint64_t position = reservation_offset;
    while (Fdt__range_valid(position, 2U * sizeof(uint64_t), total_size))
    {
        uint64_t base = Fdt__read_u64(blob + position);
        uint64_t size = Fdt__read_u64(blob + position + sizeof(uint64_t));
        if (base == 0 && size == 0)
        {
            return RISCV_DEVICE_TREE_OK;
        }

        RiscVDeviceTreeStatus status = RiscVPlatformInfo__reserve(platform, base, size);
        if (status != RISCV_DEVICE_TREE_OK)
        {
            return status;
        }
        position += 2U * sizeof(uint64_t);
    }
    return RISCV_DEVICE_TREE_TRUNCATED;
}

static FdtRegister FdtProperty__read_reg(ConstFdtBytesPtr value, uint64_t length, uint32_t address_cells,
                                         uint32_t size_cells)
{
    FdtRegister result = FdtRegister__new();
    if ((address_cells != 1 && address_cells != 2) || size_cells > 2)
    {
        return result;
    }

    uint64_t required = ((uint64_t)address_cells + size_cells) * sizeof(uint32_t);
    if (length < required)
    {
        return result;
    }

    uint64_t base = 0;
    for (uint32_t index = 0; index < address_cells; index++)
    {
        base = (base << 32) | Fdt__read_u32(value + ((uint64_t)index * sizeof(uint32_t)));
    }

    uint64_t size = 0;
    for (uint32_t index = 0; index < size_cells; index++)
    {
        uint64_t cell = (uint64_t)address_cells + index;
        size = (size << 32) | Fdt__read_u32(value + (cell * sizeof(uint32_t)));
    }

    result.present = true;
    result.base = base;
    result.size = size;
    return result;
}

static RiscVDeviceTreeStatus FdtNode__read_property(FdtNodePtr self, ConstFdtBytesPtr name, uint64_t name_available,
                                                    ConstFdtBytesPtr value, uint64_t length)
{
    if (FdtProperty__name_equals(name, name_available, "#address-cells", 14))
    {
        if (length != sizeof(uint32_t))
        {
            return RISCV_DEVICE_TREE_MALFORMED;
        }
        self->child_address_cells = Fdt__read_u32(value);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "#size-cells", 11))
    {
        if (length != sizeof(uint32_t))
        {
            return RISCV_DEVICE_TREE_MALFORMED;
        }
        self->child_size_cells = Fdt__read_u32(value);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "reg", 3))
    {
        self->reg = FdtProperty__read_reg(value, length, self->parent_address_cells, self->parent_size_cells);
        return self->reg.present ? RISCV_DEVICE_TREE_OK : RISCV_DEVICE_TREE_UNSUPPORTED_CELLS;
    }

    if (FdtProperty__name_equals(name, name_available, "device_type", 11))
    {
        self->memory = FdtStringList__contains(value, length, "memory", 6);
        self->cpu = FdtStringList__contains(value, length, "cpu", 3);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "compatible", 10))
    {
        self->uart = FdtStringList__contains(value, length, "ns16550a", 8);
        self->rtc = FdtStringList__contains(value, length, "google,goldfish-rtc", 19);
        self->plic = FdtStringList__contains(value, length, "sifive,plic-1.0.0", 17) ||
                     FdtStringList__contains(value, length, "riscv,plic0", 11);
        self->virtio_mmio = FdtStringList__contains(value, length, "virtio,mmio", 11);
        self->pci_ecam = FdtStringList__contains(value, length, "pci-host-ecam-generic", 21);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "status", 6))
    {
        self->enabled = !FdtStringList__contains(value, length, "disabled", 8);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "interrupts", 10) && length >= sizeof(uint32_t))
    {
        self->has_interrupt = true;
        self->interrupt = Fdt__read_u32(value);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "riscv,ndev", 10) && length == sizeof(uint32_t))
    {
        self->has_plic_source_count = true;
        self->plic_source_count = Fdt__read_u32(value);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "timebase-frequency", 18) && length == sizeof(uint32_t))
    {
        self->has_timebase_frequency = true;
        self->timebase_frequency = Fdt__read_u32(value);
        return RISCV_DEVICE_TREE_OK;
    }

    if (FdtProperty__name_equals(name, name_available, "bus-range", 9) && length >= 2U * sizeof(uint32_t))
    {
        self->has_bus_range = true;
        self->bus_start = Fdt__read_u32(value);
        self->bus_end = Fdt__read_u32(value + sizeof(uint32_t));
    }
    return RISCV_DEVICE_TREE_OK;
}

static RiscVDeviceTreeStatus FdtNode__finish(FdtNodePtr self, RiscVPlatformInfoPtr platform)
{
    if (!self->enabled)
    {
        return RISCV_DEVICE_TREE_OK;
    }

    if (self->has_timebase_frequency)
    {
        platform->timebase_frequency = self->timebase_frequency;
    }
    if (self->cpu)
    {
        platform->cpu_count++;
    }
    if (self->memory && self->reg.present)
    {
        platform->memory_base = self->reg.base;
        platform->memory_size = self->reg.size;
    }
    if (self->uart && self->reg.present && self->has_interrupt)
    {
        platform->uart_base = self->reg.base;
        platform->uart_size = self->reg.size;
        platform->uart_irq = self->interrupt;
    }
    if (self->rtc && self->reg.present && self->has_interrupt)
    {
        platform->rtc_base = self->reg.base;
        platform->rtc_size = self->reg.size;
        platform->rtc_irq = self->interrupt;
    }
    if (self->plic && self->reg.present && self->has_plic_source_count)
    {
        platform->plic_base = self->reg.base;
        platform->plic_size = self->reg.size;
        platform->plic_source_count = self->plic_source_count;
    }
    if (self->virtio_mmio && self->reg.present && self->has_interrupt)
    {
        if (platform->virtio_mmio_count == 0 || self->reg.base < platform->virtio_mmio_base)
        {
            platform->virtio_mmio_base = self->reg.base;
            platform->virtio_mmio_size = self->reg.size;
            platform->virtio_mmio_first_irq = self->interrupt;
        }
        platform->virtio_mmio_count++;
    }
    if (self->pci_ecam && self->reg.present && self->has_bus_range)
    {
        platform->pci_ecam_base = self->reg.base;
        platform->pci_ecam_size = self->reg.size;
        platform->pci_bus_start = self->bus_start;
        platform->pci_bus_end = self->bus_end;
    }
    if (self->reserved_memory_child && self->reg.present)
    {
        return RiscVPlatformInfo__reserve(platform, self->reg.base, self->reg.size);
    }
    return RISCV_DEVICE_TREE_OK;
}

static RiscVDeviceTreeStatus RiscVPlatformInfo__validate(ConstRiscVPlatformInfoPtr self)
{
    if (self->memory_size == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_MEMORY;
    }
    if (self->memory_base > UINT64_MAX - self->memory_size)
    {
        return RISCV_DEVICE_TREE_MALFORMED;
    }
    if (self->cpu_count == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_CPUS;
    }
    if (self->timebase_frequency == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_TIMEBASE;
    }
    if (self->uart_size == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_UART;
    }
    if (self->plic_size == 0 || self->plic_source_count == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_PLIC;
    }
    if (self->virtio_mmio_count == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_VIRTIO;
    }
    if (self->pci_ecam_size == 0)
    {
        return RISCV_DEVICE_TREE_MISSING_PCI;
    }
    return RISCV_DEVICE_TREE_OK;
}

RiscVDeviceTreeResult RiscVDeviceTree__parse(uintptr_t device_tree)
{
    RiscVPlatformInfo platform = RiscVPlatformInfo__new();
    if (device_tree == 0)
    {
        return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_INVALID_HEADER, &platform);
    }
    ConstFdtBytesPtr blob = (ConstFdtBytesPtr)device_tree;
    if (Fdt__read_u32(blob) != FDT_MAGIC)
    {
        return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_INVALID_HEADER, &platform);
    }

    uint64_t total_size = Fdt__read_u32(blob + 4);
    uint64_t structure_offset = Fdt__read_u32(blob + 8);
    uint64_t strings_offset = Fdt__read_u32(blob + 12);
    uint64_t reservation_offset = Fdt__read_u32(blob + 16);
    uint64_t strings_size = Fdt__read_u32(blob + 32);
    uint64_t structure_size = Fdt__read_u32(blob + 36);
    if (total_size < FDT_HEADER_SIZE || !Fdt__range_valid(structure_offset, structure_size, total_size) ||
        !Fdt__range_valid(strings_offset, strings_size, total_size))
    {
        return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TRUNCATED, &platform);
    }

    RiscVDeviceTreeStatus reservation_status =
        Fdt__read_reservation_map(blob, total_size, reservation_offset, &platform);
    if (reservation_status != RISCV_DEVICE_TREE_OK)
    {
        return RiscVDeviceTreeResult__new(reservation_status, &platform);
    }

    ConstFdtBytesPtr structure = blob + structure_offset;
    ConstFdtBytesPtr strings = blob + strings_offset;
    uint64_t position = 0;
    int32_t depth = -1;
    FdtNode nodes[FDT_MAX_DEPTH];

    while (Fdt__range_valid(position, sizeof(uint32_t), structure_size))
    {
        uint32_t token = Fdt__read_u32(structure + position);
        position += sizeof(uint32_t);

        if (token == FDT_BEGIN_NODE)
        {
            depth++;
            if (depth >= FDT_MAX_DEPTH)
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TOO_DEEP, &platform);
            }

            uint64_t name_length = 0;
            while (position + name_length < structure_size && structure[position + name_length] != 0)
            {
                name_length++;
            }
            if (position + name_length >= structure_size)
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TRUNCATED, &platform);
            }
            ConstFdtBytesPtr node_name = structure + position;
            position = Fdt__align_u32(position + name_length + 1U);

            uint32_t address_cells = depth > 0 ? nodes[depth - 1].child_address_cells : 2U;
            uint32_t size_cells = depth > 0 ? nodes[depth - 1].child_size_cells : 1U;
            bool reserved_memory_child = depth > 0 && nodes[depth - 1].reserved_memory_container;
            nodes[depth] = FdtNode__new(address_cells, size_cells);
            nodes[depth].reserved_memory_container = FdtText__equals(node_name, name_length, "reserved-memory", 15);
            nodes[depth].reserved_memory_child = reserved_memory_child;
            continue;
        }

        if (token == FDT_END_NODE)
        {
            if (depth < 0)
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_MALFORMED, &platform);
            }
            RiscVDeviceTreeStatus status = FdtNode__finish(&nodes[depth], &platform);
            if (status != RISCV_DEVICE_TREE_OK)
            {
                return RiscVDeviceTreeResult__new(status, &platform);
            }
            depth--;
            continue;
        }

        if (token == FDT_PROP)
        {
            if (depth < 0 || !Fdt__range_valid(position, 2U * sizeof(uint32_t), structure_size))
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TRUNCATED, &platform);
            }
            uint64_t length = Fdt__read_u32(structure + position);
            uint64_t name_offset = Fdt__read_u32(structure + position + sizeof(uint32_t));
            position += 2U * sizeof(uint32_t);
            if (!Fdt__range_valid(position, length, structure_size) || name_offset >= strings_size)
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TRUNCATED, &platform);
            }

            RiscVDeviceTreeStatus status = FdtNode__read_property(
                &nodes[depth], strings + name_offset, strings_size - name_offset, structure + position, length);
            if (status != RISCV_DEVICE_TREE_OK)
            {
                return RiscVDeviceTreeResult__new(status, &platform);
            }
            position = Fdt__align_u32(position + length);
            continue;
        }

        if (token == FDT_NOP)
        {
            continue;
        }

        if (token == FDT_END)
        {
            if (depth != -1)
            {
                return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_MALFORMED, &platform);
            }
            RiscVDeviceTreeStatus status = RiscVPlatformInfo__validate(&platform);
            return RiscVDeviceTreeResult__new(status, &platform);
        }

        return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_MALFORMED, &platform);
    }

    return RiscVDeviceTreeResult__new(RISCV_DEVICE_TREE_TRUNCATED, &platform);
}

#include "system_info.h"

FixedArrayStackBodyDefine(PhysicalMemoryRegions);

bool PhysicalMemoryRegion__equals(void * a, void * b)
{
    PhysicalMemoryRegionPtr left = a;
    PhysicalMemoryRegionPtr right = b;
    return left->physical_start == right->physical_start && left->physical_end == right->physical_end;
}

static bool MemoryRegion__contains_physical(MemoryRegionPtr self, Address physical)
{
    return physical >= self->physical_start && physical < self->physical_end;
}

static bool MemoryRegion__contains_virtual(MemoryRegionPtr self, Address virtual_addr)
{
    Address virtual_end = self->virtual_start + (self->physical_end - self->physical_start);
    return virtual_addr >= self->virtual_start && virtual_addr < virtual_end;
}

static KernelAddress MemoryRegion__kernel_address_for_physical(MemoryRegionPtr self, Address physical)
{
    return KERNEL_ADDR(self->virtual_start + (physical - self->physical_start), physical);
}

static KernelAddress MemoryRegion__kernel_address_for_virtual(MemoryRegionPtr self, Address virtual_addr)
{
    return KERNEL_ADDR(virtual_addr, self->physical_start + (virtual_addr - self->virtual_start));
}

KernelAddress SystemInfo__kernel_address_for_physical(SystemInfoPtr self, Address physical)
{
    if (MemoryRegion__contains_physical(&self->kernel_image, physical))
    {
        return MemoryRegion__kernel_address_for_physical(&self->kernel_image, physical);
    }

    if (MemoryRegion__contains_physical(&self->kernel_heap, physical))
    {
        return MemoryRegion__kernel_address_for_physical(&self->kernel_heap, physical);
    }

    if (MemoryRegion__contains_physical(&self->kernel_stack, physical))
    {
        return MemoryRegion__kernel_address_for_physical(&self->kernel_stack, physical);
    }

    if (MemoryRegion__contains_physical(&self->embedded_binaries, physical))
    {
        return MemoryRegion__kernel_address_for_physical(&self->embedded_binaries, physical);
    }

    return INVALID_KERNEL_ADDR;
}

KernelAddress SystemInfo__kernel_address_for_virtual(SystemInfoPtr self, Address virtual_addr)
{
    if (MemoryRegion__contains_virtual(&self->kernel_image, virtual_addr))
    {
        return MemoryRegion__kernel_address_for_virtual(&self->kernel_image, virtual_addr);
    }

    if (MemoryRegion__contains_virtual(&self->kernel_heap, virtual_addr))
    {
        return MemoryRegion__kernel_address_for_virtual(&self->kernel_heap, virtual_addr);
    }

    if (MemoryRegion__contains_virtual(&self->kernel_stack, virtual_addr))
    {
        return MemoryRegion__kernel_address_for_virtual(&self->kernel_stack, virtual_addr);
    }

    if (MemoryRegion__contains_virtual(&self->embedded_binaries, virtual_addr))
    {
        return MemoryRegion__kernel_address_for_virtual(&self->embedded_binaries, virtual_addr);
    }

    return INVALID_KERNEL_ADDR;
}

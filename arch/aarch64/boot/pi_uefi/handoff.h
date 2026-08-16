#ifndef __AARCH64_BOOT_PI_UEFI_HANDOFF_H_
#define __AARCH64_BOOT_PI_UEFI_HANDOFF_H_

#include <stdint.h>

#define PI_UEFI_HANDOFF_MAGIC 0x504955454649484fULL
#define PI_UEFI_MAX_PHYSICAL_MEMORY_REGIONS 64

typedef struct PiUefiPhysicalMemoryRegion {
    uint64_t physical_start;
    uint64_t physical_end;
} PiUefiPhysicalMemoryRegion;

typedef struct PiUefiHandoff {
    uint64_t physical_memory_region_count;
    PiUefiPhysicalMemoryRegion physical_memory_regions[PI_UEFI_MAX_PHYSICAL_MEMORY_REGIONS];
} PiUefiHandoff;

typedef PiUefiHandoff * PiUefiHandoffPtr;

#endif

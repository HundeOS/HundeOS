#ifndef __AARCH64_BOOT_PI_UEFI_HANDOFF_H_
#define __AARCH64_BOOT_PI_UEFI_HANDOFF_H_

#include <stdint.h>

#define PI_UEFI_HANDOFF_MAGIC 0x504955454649484fULL
#define PI_UEFI_MAX_PHYSICAL_MEMORY_REGIONS 64

typedef struct PiUefiPhysicalMemoryRegion {
    uint64_t physical_start;
    uint64_t physical_end;
} PiUefiPhysicalMemoryRegion;

typedef struct PiUefiFramebuffer {
    uint64_t physical_base;
    uint64_t size;
    uint32_t width;
    uint32_t height;
    uint32_t pixels_per_scanline;
    uint32_t pixel_format;
    uint32_t red_mask;
    uint32_t green_mask;
    uint32_t blue_mask;
    uint32_t reserved_mask;
} PiUefiFramebuffer;

typedef struct PiUefiHandoff {
    uint64_t physical_memory_region_count;
    PiUefiPhysicalMemoryRegion physical_memory_regions[PI_UEFI_MAX_PHYSICAL_MEMORY_REGIONS];
    uint64_t framebuffer_present;
    PiUefiFramebuffer framebuffer;
} PiUefiHandoff;

typedef PiUefiHandoff * PiUefiHandoffPtr;

#endif

#ifndef HUNDE_RUST_DMA_H
#define HUNDE_RUST_DMA_H

#include "native.h"

typedef struct HundeDmaAllocation
{
    HundeStatusResult status;
    uint64_t address;
    uint64_t device_address;
} HundeDmaAllocation;

_Static_assert(sizeof(HundeDmaAllocation) == 48, "DMA allocation ABI changed");

HundeDmaAllocation hunde_dma_allocate(uint64_t size, uint64_t alignment);
void hunde_dma_clean(uint64_t address, uint64_t size);
void hunde_dma_invalidate(uint64_t address, uint64_t size);
void hunde_dma_clean_invalidate(uint64_t address, uint64_t size);
void hunde_dma_boundary(void);

#endif

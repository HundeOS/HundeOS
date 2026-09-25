#include "device.h"
typedef volatile uint32_t * VirtioRegisterPtr;

uint32_t VirtioBlkMmio__read32(void * base, uint64_t offset)
{
    return *(VirtioRegisterPtr)((Address)base + offset);
}

void VirtioBlkMmio__write32(void * base, uint64_t offset, uint32_t value)
{
    *(VirtioRegisterPtr)((Address)base + offset) = value;
}

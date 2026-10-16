#include "../memory.h"

#include <stdint.h>

void * ArchMemory__memcpy(void * self, const void * source, size_t size)
{
    uint8_t * destination = self;
    const uint8_t * source_bytes = source;
    for (size_t index = 0; index < size; index++)
    {
        destination[index] = source_bytes[index];
    }
    return self;
}

void * ArchMemory__memset(void * self, int value, size_t size)
{
    uint8_t * destination = self;
    for (size_t index = 0; index < size; index++)
    {
        destination[index] = (uint8_t)value;
    }
    return self;
}

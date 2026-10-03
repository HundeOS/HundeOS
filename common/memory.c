#include "memory.h"

#include <stdint.h>

void * Mem__memcpy(void * self, const void * source, size_t size)
{
    volatile uint8_t * destination_bytes = self;
    const volatile uint8_t * source_bytes = source;
    for (size_t index = 0; index < size; index++)
    {
        destination_bytes[index] = source_bytes[index];
    }
    return self;
}

void * Mem__memmove(void * self, const void * source, size_t size)
{
    volatile uint8_t * destination_bytes = self;
    const volatile uint8_t * source_bytes = source;
    uintptr_t destination_address = (uintptr_t)self;
    uintptr_t source_address = (uintptr_t)source;
    if (destination_address < source_address)
    {
        return Mem__memcpy(self, source, size);
    }
    while (size > 0)
    {
        size--;
        destination_bytes[size] = source_bytes[size];
    }
    return self;
}

void * Mem__memset(void * self, int value, size_t size)
{
    volatile uint8_t * destination_bytes = self;
    for (size_t index = 0; index < size; index++)
    {
        destination_bytes[index] = (uint8_t)value;
    }
    return self;
}

int Mem__memcmp(const void * self, const void * other, size_t size)
{
    const volatile uint8_t * self_bytes = self;
    const volatile uint8_t * other_bytes = other;
    for (size_t index = 0; index < size; index++)
    {
        if (self_bytes[index] != other_bytes[index])
        {
            return self_bytes[index] - other_bytes[index];
        }
    }
    return 0;
}

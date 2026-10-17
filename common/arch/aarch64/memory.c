#include "../memory.h"

#include <stdint.h>

void * ArchMemory__memcpy(void * self, const void * source, size_t size)
{
    uint8_t * destination = self;
    const uint8_t * source_bytes = source;
    if ((((uintptr_t)destination ^ (uintptr_t)source_bytes) & 0xfU) != 0)
    {
        while (size > 0)
        {
            *destination = *source_bytes;
            destination++;
            source_bytes++;
            size--;
        }
        return self;
    }

    while (size > 0 && ((uintptr_t)destination & 0xfU) != 0)
    {
        *destination = *source_bytes;
        destination++;
        source_bytes++;
        size--;
    }
    while (size >= 64)
    {
        uint64_t word0;
        uint64_t word1;
        uint64_t word2;
        uint64_t word3;
        uint64_t word4;
        uint64_t word5;
        uint64_t word6;
        uint64_t word7;
        __asm__ volatile("ldp %x[word0], %x[word1], [%[source], #0]\n"
                         "ldp %x[word2], %x[word3], [%[source], #16]\n"
                         "ldp %x[word4], %x[word5], [%[source], #32]\n"
                         "ldp %x[word6], %x[word7], [%[source], #48]\n"
                         "stp %x[word0], %x[word1], [%[destination], #0]\n"
                         "stp %x[word2], %x[word3], [%[destination], #16]\n"
                         "stp %x[word4], %x[word5], [%[destination], #32]\n"
                         "stp %x[word6], %x[word7], [%[destination], #48]\n"
                         : [word0] "=&r"(word0), [word1] "=&r"(word1), [word2] "=&r"(word2), [word3] "=&r"(word3),
                           [word4] "=&r"(word4), [word5] "=&r"(word5), [word6] "=&r"(word6), [word7] "=&r"(word7)
                         : [destination] "r"(destination), [source] "r"(source_bytes)
                         : "memory");
        destination += 64;
        source_bytes += 64;
        size -= 64;
    }
    while (size >= 16)
    {
        uint64_t word0;
        uint64_t word1;
        __asm__ volatile("ldp %x[word0], %x[word1], [%[source]]\n"
                         "stp %x[word0], %x[word1], [%[destination]]"
                         : [word0] "=&r"(word0), [word1] "=&r"(word1)
                         : [destination] "r"(destination), [source] "r"(source_bytes)
                         : "memory");
        destination += 16;
        source_bytes += 16;
        size -= 16;
    }
    if (size >= 8)
    {
        uint64_t word;
        __asm__ volatile("ldr %x[word], [%[source]]\n"
                         "str %x[word], [%[destination]]"
                         : [word] "=&r"(word)
                         : [destination] "r"(destination), [source] "r"(source_bytes)
                         : "memory");
        destination += 8;
        source_bytes += 8;
        size -= 8;
    }
    while (size > 0)
    {
        *destination = *source_bytes;
        destination++;
        source_bytes++;
        size--;
    }
    return self;
}

static uint64_t ArchMemory__repeated_byte(int value)
{
    return (uint64_t)(uint8_t)value * 0x0101010101010101ULL;
}

void * ArchMemory__memset(void * self, int value, size_t size)
{
    uint8_t * cursor = self;
    while (size > 0 && ((uintptr_t)cursor & 0xfU) != 0)
    {
        *cursor = (uint8_t)value;
        cursor++;
        size--;
    }

    uint64_t repeated = ArchMemory__repeated_byte(value);
    while (size >= 64)
    {
        __asm__ volatile("stp %x[repeated], %x[repeated], [%[cursor], #0]\n"
                         "stp %x[repeated], %x[repeated], [%[cursor], #16]\n"
                         "stp %x[repeated], %x[repeated], [%[cursor], #32]\n"
                         "stp %x[repeated], %x[repeated], [%[cursor], #48]\n"
                         :
                         : [cursor] "r"(cursor), [repeated] "r"(repeated)
                         : "memory");
        cursor += 64;
        size -= 64;
    }
    while (size >= 16)
    {
        __asm__ volatile("stp %x[repeated], %x[repeated], [%[cursor]]"
                         :
                         : [cursor] "r"(cursor), [repeated] "r"(repeated)
                         : "memory");
        cursor += 16;
        size -= 16;
    }
    if (size >= 8)
    {
        __asm__ volatile("str %x[repeated], [%[cursor]]" : : [cursor] "r"(cursor), [repeated] "r"(repeated) : "memory");
        cursor += 8;
        size -= 8;
    }
    while (size > 0)
    {
        *cursor = (uint8_t)value;
        cursor++;
        size--;
    }
    return self;
}

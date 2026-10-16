#include "../memory.h"

#include <stdint.h>

void * ArchMemory__memcpy(void * self, const void * source, size_t size)
{
    uint8_t * destination = self;
    const uint8_t * source_bytes = source;
    if ((((uintptr_t)destination ^ (uintptr_t)source_bytes) & 0x7U) != 0)
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

    while (size > 0 && ((uintptr_t)destination & 0x7U) != 0)
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
        __asm__ volatile(
            "ld %[word0], 0(%[source])\n"
            "ld %[word1], 8(%[source])\n"
            "ld %[word2], 16(%[source])\n"
            "ld %[word3], 24(%[source])\n"
            "ld %[word4], 32(%[source])\n"
            "ld %[word5], 40(%[source])\n"
            "ld %[word6], 48(%[source])\n"
            "ld %[word7], 56(%[source])\n"
            "sd %[word0], 0(%[destination])\n"
            "sd %[word1], 8(%[destination])\n"
            "sd %[word2], 16(%[destination])\n"
            "sd %[word3], 24(%[destination])\n"
            "sd %[word4], 32(%[destination])\n"
            "sd %[word5], 40(%[destination])\n"
            "sd %[word6], 48(%[destination])\n"
            "sd %[word7], 56(%[destination])\n"
            : [word0] "=&r"(word0), [word1] "=&r"(word1), [word2] "=&r"(word2),
              [word3] "=&r"(word3), [word4] "=&r"(word4), [word5] "=&r"(word5),
              [word6] "=&r"(word6), [word7] "=&r"(word7)
            : [destination] "r"(destination), [source] "r"(source_bytes)
            : "memory"
        );
        destination += 64;
        source_bytes += 64;
        size -= 64;
    }
    while (size >= 8)
    {
        uint64_t word;
        __asm__ volatile(
            "ld %[word], 0(%[source])\n"
            "sd %[word], 0(%[destination])"
            : [word] "=&r"(word)
            : [destination] "r"(destination), [source] "r"(source_bytes)
            : "memory"
        );
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
    while (size > 0 && ((uintptr_t)cursor & 0x7U) != 0)
    {
        *cursor = (uint8_t)value;
        cursor++;
        size--;
    }

    uint64_t repeated = ArchMemory__repeated_byte(value);
    while (size >= 64)
    {
        __asm__ volatile(
            "sd %[repeated], 0(%[cursor])\n"
            "sd %[repeated], 8(%[cursor])\n"
            "sd %[repeated], 16(%[cursor])\n"
            "sd %[repeated], 24(%[cursor])\n"
            "sd %[repeated], 32(%[cursor])\n"
            "sd %[repeated], 40(%[cursor])\n"
            "sd %[repeated], 48(%[cursor])\n"
            "sd %[repeated], 56(%[cursor])\n"
            :
            : [cursor] "r"(cursor), [repeated] "r"(repeated)
            : "memory"
        );
        cursor += 64;
        size -= 64;
    }
    while (size >= 8)
    {
        __asm__ volatile("sd %[repeated], 0(%[cursor])"
                         :
                         : [cursor] "r"(cursor), [repeated] "r"(repeated)
                         : "memory");
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

#ifndef COMMON_TYPES_BYTE_RING_H
#define COMMON_TYPES_BYTE_RING_H

#include <stddef.h>
#include <stdint.h>

#include "result.h"

typedef struct ByteRing
{
    uint8_t * storage;
    size_t capacity;
    size_t read_position;
    size_t count;
} ByteRing;

typedef ByteRing * ByteRingPtr;
typedef const ByteRing * ConstByteRingPtr;

ResultDef(size_t);

ByteRing ByteRing__new(uint8_t * storage, size_t capacity);
size_t ByteRing__count(ConstByteRingPtr self);
size_t ByteRing__free_space(ConstByteRingPtr self);
Result(size_t) ByteRing__push(ByteRingPtr self, const uint8_t * bytes, size_t count);
size_t ByteRing__drain(ByteRingPtr self, uint8_t * bytes, size_t count);

#endif

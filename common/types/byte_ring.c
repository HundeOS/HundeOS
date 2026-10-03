#include "byte_ring.h"

static BString byte_ring_full = bstr("Byte ring does not have enough space");

ByteRing ByteRing__new(uint8_t * storage, size_t capacity)
{
    return (ByteRing){.storage = storage, .capacity = capacity};
}

size_t ByteRing__count(ConstByteRingPtr self)
{
    return self->count;
}

size_t ByteRing__free_space(ConstByteRingPtr self)
{
    return self->capacity - self->count;
}

Result(size_t) ByteRing__push(ByteRingPtr self, const uint8_t * bytes, size_t count)
{
    if (count == 0)
    {
        return Success(size_t, 0);
    }
    if (count > ByteRing__free_space(self))
    {
        return Error(size_t, byte_ring_full, -1);
    }
    size_t write_position = (self->read_position + self->count) % self->capacity;
    for (size_t index = 0; index < count; index++)
    {
        self->storage[(write_position + index) % self->capacity] = bytes[index];
    }
    self->count += count;
    return Success(size_t, count);
}

size_t ByteRing__drain(ByteRingPtr self, uint8_t * bytes, size_t count)
{
    size_t drained = count < self->count ? count : self->count;
    if (drained == 0)
    {
        return 0;
    }
    for (size_t index = 0; index < drained; index++)
    {
        bytes[index] = self->storage[(self->read_position + index) % self->capacity];
    }
    self->read_position = (self->read_position + drained) % self->capacity;
    self->count -= drained;
    return drained;
}

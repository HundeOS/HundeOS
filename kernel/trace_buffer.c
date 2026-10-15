#include "trace_buffer.h"

#include "memory.h"

static BString kernel_trace_busy = bstr("Kernel trace is busy");
static KernelTraceBuffer kernel_trace = KERNEL_TRACE_BUFFER_INIT;

static bool KernelTraceBuffer__try_lock(KernelTraceBufferPtr self)
{
    return !atomic_flag_test_and_set_explicit(&self->lock, memory_order_acquire);
}

static void KernelTraceBuffer__unlock(KernelTraceBufferPtr self)
{
    atomic_flag_clear_explicit(&self->lock, memory_order_release);
}

void KernelTraceBuffer__append(KernelTraceBufferPtr self, const char * text)
{
    if (!KernelTraceBuffer__try_lock(self))
    {
        return;
    }

    size_t length = 0;
    while (text[length] != '\0')
    {
        length++;
    }

    if (length >= KERNEL_TRACE_CAPACITY)
    {
        text += length - KERNEL_TRACE_CAPACITY;
        length = KERNEL_TRACE_CAPACITY;
        self->write_position = 0;
        self->count = 0;
    }

    size_t first = length;
    if (first > KERNEL_TRACE_CAPACITY - self->write_position)
    {
        first = KERNEL_TRACE_CAPACITY - self->write_position;
    }
    Mem__memcpy(self->storage + self->write_position, text, first);
    Mem__memcpy(self->storage, text + first, length - first);
    self->write_position = (self->write_position + length) % KERNEL_TRACE_CAPACITY;
    self->count = self->count + length < KERNEL_TRACE_CAPACITY ? self->count + length : KERNEL_TRACE_CAPACITY;

    KernelTraceBuffer__unlock(self);
}

Result(KernelTraceSize) KernelTraceBuffer__snapshot(KernelTraceBufferPtr self, uint8_t * destination, size_t capacity)
{
    if (!KernelTraceBuffer__try_lock(self))
    {
        return Error(KernelTraceSize, kernel_trace_busy, SYSTEM_INFO_UNAVAILABLE);
    }

    size_t size = self->count < capacity ? self->count : capacity;
    size_t oldest = (self->write_position + KERNEL_TRACE_CAPACITY - self->count) % KERNEL_TRACE_CAPACITY;
    size_t skip = self->count - size;
    size_t start = (oldest + skip) % KERNEL_TRACE_CAPACITY;
    size_t first = size;
    if (first > KERNEL_TRACE_CAPACITY - start)
    {
        first = KERNEL_TRACE_CAPACITY - start;
    }
    Mem__memcpy(destination, self->storage + start, first);
    Mem__memcpy(destination + first, self->storage, size - first);

    KernelTraceBuffer__unlock(self);
    return Success(KernelTraceSize, size);
}

void KernelTrace__append(const char * text)
{
    KernelTraceBuffer__append(&kernel_trace, text);
}

Result(KernelTraceSize) KernelTrace__snapshot(uint8_t * destination, size_t capacity)
{
    return KernelTraceBuffer__snapshot(&kernel_trace, destination, capacity);
}

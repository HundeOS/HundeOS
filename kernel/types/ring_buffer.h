#ifndef __KERNEL_TYPES_RING_BUFFER_H
#define __KERNEL_TYPES_RING_BUFFER_H

#include "types.h"
#include "result.h"
#include "bstring.h"
#include "../address.h"
#include "../debug.h"

KernelAddress Kernel__alloc(size_t size);

typedef size_t Pos;
typedef size_t ItemCount;

ResultDef(Pos);
ResultDef(ItemCount);

static BString OVERFLOW_ERROR = bstr("Ring buffer overflow");
static BString OVERFLOW_AVOIDED_ERROR = bstr("Ring buffer overflow would occur");
static BString UNDERFLOW_ERROR = bstr("Ring buffer underflow");

#ifndef RING_BUFFER_DEBUG_PRINTF
#define RING_BUFFER_DEBUG_PRINTF(level, str, ...) __d_printf(level, str, ##__VA_ARGS__)
#endif

typedef enum RingBufferError {
    RING_BUFFER_OVERFLOW = -1,
    RING_BUFFER_UNDERFLOW = -2,
} RingBufferError;

#define EmptyRingBuffer(alias) \
    (alias) { \
        .read_pos = 0, \
        .write_pos = 0, \
        .buffer_size = RingBuffer__##alias##__size, \
    } \

#define for_each_ring_buffer(alias, ring_buffer, index, value, ...) \
    for (size_t index = 0; index < alias##__item_count(&(ring_buffer)); index++) \
    { \
        Char value = (ring_buffer).buffer[((ring_buffer).read_pos + index) % (ring_buffer).buffer_size]; \
        __VA_ARGS__ \
    }

#define for_each_ring_buffer_ptr(alias, ring_buffer, index, value, ...) \
    for (size_t index = 0; index < alias##__item_count(ring_buffer); index++) \
    { \
        Char value = (ring_buffer)->buffer[((ring_buffer)->read_pos + index) % (ring_buffer)->buffer_size]; \
        __VA_ARGS__ \
    }

#define RingBufferDefine(size, alias) \
    static const size_t RingBuffer__##alias##__size = size; \
    typedef struct RingBuffer__##size \
    { \
        size_t read_pos; \
        size_t write_pos; \
        size_t buffer_size; \
        Char buffer[size]; \
    } alias; \
    \
    typedef alias * alias##Ptr;\
    \
    RingBufferConstructorPreDefine(alias) \
    RingBufferPushPreDefine(alias) \
    RingBufferPopPreDefine(alias) \
    RingBufferDrainPreDefine(alias) \
    RingBufferResetPreDefine(alias) \
    RingBufferItemCountPreDefine(alias) \


#define RingBufferBodyDefine(alias) \
    RingBufferConstructorDefine(alias) \
    RingBufferPushDefine(alias) \
    RingBufferPopDefine(alias) \
    RingBufferDrainDefine(alias) \
    RingBufferResetDefine(alias) \
    RingBufferItemCountDefine(alias) \

#define RingBufferStackBodyDefine(alias) \
    RingBufferStackConstructorDefine(alias) \
    RingBufferPushDefine(alias) \
    RingBufferPopDefine(alias) \
    RingBufferDrainDefine(alias) \
    RingBufferResetDefine(alias) \
    RingBufferItemCountDefine(alias) \


#define RingBufferConstructorPreDefine(alias) \
    alias alias##__new(); \
    alias##Ptr alias##__heap_new(); \

#define RingBufferConstructorDefine(alias) \
    RingBufferStackConstructorDefine(alias) \
    RingBufferHeapConstructorDefine(alias) \

#define RingBufferStackConstructorDefine(alias) \
    alias alias##__new() \
    { \
        return (alias) { \
            .read_pos = 0, \
            .write_pos = 0, \
            .buffer_size = RingBuffer__##alias##__size,\
        }; \
    } \

#define RingBufferHeapConstructorDefine(alias) \
    alias##Ptr alias##__heap_new() \
    { \
        alias##Ptr buf = v_ptr(Kernel__alloc(sizeof(alias)), alias); \
        __d_printf(DEBUG, "Allocated %p\n", buf); \
        buf->read_pos = 0; \
        buf->write_pos = 0; \
        buf->buffer_size = RingBuffer__##alias##__size; \
        \
        return buf;\
    } \

#define RingBufferPushPreDefine(alias) \
    Result(Pos) alias##__push(alias##Ptr self, Char value); \
    Result(Pos) alias##__push_all(alias##Ptr self, Char values[], size_t size); \

#define RingBufferPushDefine(alias) \
    Result(Pos) alias##__push(alias##Ptr self, Char value) \
    { \
        \
        if ((self->write_pos - self->read_pos) >= self->buffer_size) \
        { \
            RING_BUFFER_DEBUG_PRINTF(DEBUG, "overflow %i %i\n", self->write_pos, self->read_pos); \
            return Error(Pos, OVERFLOW_ERROR, RING_BUFFER_OVERFLOW); \
        } \
        \
        Pos pos = self->write_pos % self->buffer_size; \
        self->buffer[pos] = value; \
        self->write_pos++; \
        \
        return Success(Pos, pos); \
    } \
    \
    Result(Pos) alias##__push_all(alias##Ptr self, Char values[], size_t size) \
    { \
        \
        if ((self->write_pos - self->read_pos) >= self->buffer_size) \
        { \
            RING_BUFFER_DEBUG_PRINTF(DEBUG, "overflow %i %i\n", self->write_pos, self->read_pos); \
            return Error(Pos, OVERFLOW_ERROR, RING_BUFFER_OVERFLOW); \
        } \
        \
        if ((self->write_pos + (size - 1) - self->read_pos) >= self->buffer_size) \
        { \
            RING_BUFFER_DEBUG_PRINTF(DEBUG, "overflow %i %i\n", self->write_pos + size, self->read_pos); \
            return Error(Pos, OVERFLOW_AVOIDED_ERROR, RING_BUFFER_OVERFLOW); \
        } \
        \
        Pos pos = self->write_pos % self->buffer_size;\
        for (Pos i = 0; i < size; i++) \
        { \
            Pos write_pos = (pos + i) % self->buffer_size; \
            self->buffer[write_pos] = values[i];\
        } \
        self->write_pos += size; \
        \
        return Success(Pos, pos); \
    } \

#define RingBufferPopPreDefine(alias) \
    Result(Char) alias##__pop(alias##Ptr self); \

#define RingBufferPopDefine(alias) \
    Result(Char) alias##__pop(alias##Ptr self) \
    { \
        if ((self->write_pos - self->read_pos) <= 0) \
        { \
            RING_BUFFER_DEBUG_PRINTF(DEBUG, "overflow %i %i\n", self->write_pos, self->read_pos); \
            return Error(Char, UNDERFLOW_ERROR, RING_BUFFER_UNDERFLOW); \
        } \
        Pos pos = self->read_pos % self->buffer_size; \
        Char val = self->buffer[pos]; \
        self->read_pos++; \
        \
        return Success(Char, val);\
    } \

#define RingBufferResetPreDefine(alias) \
    Result(NoType) alias##__reset(alias##Ptr self); \

#define RingBufferResetDefine(alias) \
    Result(NoType) alias##__reset(alias##Ptr self) \
    { \
        self->read_pos = 0;\
        self->write_pos = 0; \
        return Success(NoType, Nothing);\
    } \

#define RingBufferItemCountPreDefine(alias) \
    size_t alias##__item_count(alias##Ptr self); \
    size_t alias##__free_space(alias##Ptr self); \

#define RingBufferItemCountDefine(alias) \
    size_t alias##__item_count(alias##Ptr self) \
    { \
        return self->write_pos - self->read_pos;\
    } \
    \
    size_t alias##__free_space(alias##Ptr self) \
    { \
        return self->buffer_size - alias##__item_count(self);\
    } \

#define RingBufferDrainPreDefine(alias) \
    Result(ItemCount) alias##__drain(alias##Ptr self, CharPtr drain, size_t max); \

#define RingBufferDrainDefine(alias) \
    Result(ItemCount) alias##__drain(alias##Ptr self, CharPtr drain, size_t max) \
    { \
        for (size_t i = 0; i < max; i++) \
        {\
            match_result(Char, alias##__pop(self), \
                success(val) { drain[i] = val;; }, \
                error(code, message) { \
                { \
                    return Success(ItemCount, i);\
                }; } \
            ); \
        } \
        return Success(ItemCount, max);\
    } \

#endif

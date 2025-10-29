#ifndef __KERNEL_TYPES_RING_BUFFER_H
#define __KERNEL_TYPES_RING_BUFFER_H

#include "types.h"
#include "result.h"
#include "kstring.h"

typedef size_t Pos;
typedef size_t ItemCount;

ResultDef(Pos);
ResultDef(ItemCount);

static const char * OVERFLOW_ERROR = "Ring buffer overflow";
static const char * UNDERFLOW_ERROR = "Ring buffer underflow";

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


#define RingBufferConstructorPreDefine(alias) \
    alias alias##__new(); \
    alias##Ptr alias##__heap_new(); \

#define RingBufferConstructorDefine(alias) \
    alias alias##__new() \
    { \
        return (alias) { \
            .read_pos = 0, \
            .write_pos = 0, \
            .buffer_size = RingBuffer__##alias##__size,\
        }; \
    } \
    \
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

#define RingBufferPushDefine(alias) \
    Result(Pos) alias##__push(alias##Ptr self, Char value) \
    { \
        \
        if ((self->write_pos - self->read_pos) >= self->buffer_size) \
        { \
            __d_printf(DEBUG, "overflow %i %i\n", self->write_pos, self->read_pos); \
            return Error(Pos, kstring(OVERFLOW_ERROR), RING_BUFFER_OVERFLOW); \
        } \
        \
        Pos pos = self->write_pos % self->buffer_size; \
        self->buffer[pos] = value; \
        self->write_pos++; \
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
            __d_printf(DEBUG, "overflow %i %i\n", self->write_pos, self->read_pos); \
            return Error(Char, kstring(UNDERFLOW_ERROR), RING_BUFFER_UNDERFLOW); \
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

#define RingBufferItemCountDefine(alias) \
    size_t alias##__item_count(alias##Ptr self) \
    { \
        return self->write_pos - self->read_pos;\
    } \

#define RingBufferDrainPreDefine(alias) \
    Result(ItemCount) alias##__drain(alias##Ptr self, CharPtr drain, size_t max); \

#define RingBufferDrainDefine(alias) \
    Result(ItemCount) alias##__drain(alias##Ptr self, CharPtr drain, size_t max) \
    { \
        for (int i = 0; i < max; i++) \
        {\
            ResultMatch(Char, alias##__pop(self))\
                IsSuccess(val, drain[i] = val;)\
                IsError(code, message, \
                { \
                    return Success(ItemCount, i);\
                }); \
            End \
        } \
        return Success(ItemCount, max);\
    } \

#endif
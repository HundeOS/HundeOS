#ifndef __KERNEL_SUBSYSTEMS_IO_IO_H
#define __KERNEL_SUBSYSTEMS_IO_IO_H
#include <stdint.h>

#include "../../address.h"

#include "../../types/fixed_array.h"
#include "../../types/result.h"
#include "../../types/kstring.h"

typedef uint8_t Char;
typedef Char * CharPtr;

#define _4_MEGS 4194304

OptionalDef(CharPtr);
FixedArrayDefine(CharPtr, _4_MEGS, Buffer);

typedef Buffer * BufferPtr;

typedef uint64_t IOHandle;
typedef uint64_t Flags;
typedef size_t BytesWritten;

typedef struct IO IO;
typedef IO * IOPtr;

ResultDef(IOHandle);
ResultDef(BytesWritten);
ResultDef(BufferPtr);

OptionalDef(IOPtr);
OptionalDef(BufferPtr);

typedef Result(IOHandle) (* IO_open)(KString path, Flags flags);
typedef Result(BufferPtr) (* IO_read)(IOHandle handle, size_t size);
typedef Result(BytesWritten) (* IO_write)(IOHandle handle, Optional(BufferPtr) buffer);

typedef struct IO
{
    IO_open open;
    IO_read read;
    IO_write write;
} IO;

#define NEW_BUFFER \
    (Buffer) { \
        .len = 0, \
        .max = _4_MEGS \
    }

#endif
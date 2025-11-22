#ifndef __KERNEL_EXEC_EXEC_H
#define __KERNEL_EXEC_EXEC_H
#include <stdint.h>
#include <stdint.h>

#include "../types/result.h"
#include "../types/kstring.h"
#include "../types/comparison.h"
#include "../process.h"
#include "../kernel_core.h"

typedef enum ExecError
{
    EXEC_OK = 0,
    EXEC_NOT_FOUND = 127,
    EXEC_NOT_IMPLEMENTED = 255,
    EXEC_FAILED = 155,
}
ExecError;

typedef uint64_t ExecResult;

FixedArrayDefine(Char, _16MB, ElfReadBuffer, SimpleEquals(Char));

Result(ProcessPtr) Exec__exec(OriginatingContext originating_context, KString path, bool wait);


#endif
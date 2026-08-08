#ifndef __KERNEL_EXEC_EXEC_H
#define __KERNEL_EXEC_EXEC_H
#include <stdint.h>
#include <stdint.h>

#include "../types/result.h"
#include "../types/bstring.h"
#include "../types/comparison.h"
#include "../process.h"
#include "../kernel_core.h"

typedef uint64_t ExecResult;

FixedArrayDefine(Char, _16MB, ElfReadBuffer, SimpleEquals(Char));

Result(ProcessPtr) Exec__exec(
    OriginatingContext originating_context, BString path, bool wait, ProcessArguments arguments
);
Result(ProcessPtr) Exec__exec_as(
    OriginatingContext originating_context,
    BString path,
    bool wait,
    Optional(UserID) requested_user_id,
    ProcessArguments arguments
);

void Exec__apply_builtin_identity_policy(ProcessPtr self);

#endif

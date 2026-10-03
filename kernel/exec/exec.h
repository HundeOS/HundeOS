#ifndef __KERNEL_EXEC_EXEC_H
#define __KERNEL_EXEC_EXEC_H
#include <stdint.h>
#include <stdint.h>

#include "../types/result.h"
#include "../types/bstring.h"
#include "../types/comparison.h"
#include "../process.h"
#include "../kernel_core.h"
#include "syscalls/spawn.h"

typedef uint64_t ExecResult;

bool Exec__can_execute(ConstOriginatingContextPtr context, ConstBStringPtr path);
Result(ProcessPtr) Exec__launch_loaded_elf(ConstOriginatingContextPtr context, ElfPtr elf, bool wait,
                                           OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments,
                                           ConstSpawnStdioPtr stdio, bool overlay, KernelAddress overlay_payload);

Result(ProcessPtr) Exec__exec(ConstOriginatingContextPtr originating_context, ConstBStringPtr path, bool wait,
                              ConstProcessArgumentsPtr arguments);
Result(ProcessPtr) Exec__exec_as(ConstOriginatingContextPtr originating_context, ConstBStringPtr path, bool wait,
                                 OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments);
Result(ProcessPtr)
    Exec__exec_authorized_as(ConstOriginatingContextPtr originating_context, ConstBStringPtr path, bool wait,
                             OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments);
Result(ProcessPtr)
    Exec__exec_authorized_as_with_stdio(ConstOriginatingContextPtr originating_context, ConstBStringPtr path, bool wait,
                                        OptionalPtr(UserID) requested_user_id, ConstProcessArgumentsPtr arguments,
                                        ConstSpawnStdioPtr stdio);

void Exec__apply_builtin_identity_policy(ProcessPtr self);

#endif

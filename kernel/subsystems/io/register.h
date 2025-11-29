#ifndef __KERNEL_SUBSYSTEM_IO_REGISTER_H
#define __KERNEL_SUBSYSTEM_IO_REGISTER_H
#include "../../types/result.h"
#include "io_subsystem.h"

typedef uint64_t RegisterCode;
ResultDef(RegisterCode);

Result(RegisterCode) IOSubsystem__register(KernelPtr kernel);

#endif
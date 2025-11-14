#ifndef __TEST_KERNEL_STUBS_H
#define __TEST_KERNEL_STUBS_H

#include "../../kernel/process.h"

#define STUB_PROCESS(process_id) (Process) { \
        .handles = EmptyArray(ProcessHandles),\
        .pid = process_id,\
    }\

#endif
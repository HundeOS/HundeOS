#ifndef __TEST_KERNEL_STUBS_H
#define __TEST_KERNEL_STUBS_H

#include "../../kernel/process.h"

#define STUB_PROCESS(process_id)                                                                                       \
    (Process)                                                                                                          \
    {                                                                                                                  \
        .pid = process_id, .file_descriptors = LINKED_LIST_NEW(), .next_file_descriptor = 3,                           \
    }

#endif

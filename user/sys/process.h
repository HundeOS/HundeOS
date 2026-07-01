#ifndef __USER_SYS_PROCESS_H
#define __USER_SYS_PROCESS_H

#include <stddef.h>

#include "syscalls/process.h"

ProcessListResult process_list(ProcessInfoPtr entries, size_t max_entries);

#endif

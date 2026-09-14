#ifndef __KERNEL_PROCESS_CLASS_H
#define __KERNEL_PROCESS_CLASS_H

#include <stdint.h>
#include <stdbool.h>

#include "../common/process/process_types.h"

bool ProcessClass__is_service(ProcessClass self);
uint64_t ProcessClass__priority(ProcessClass self);
const char * ProcessClass__to_string(ProcessClass self);

#endif

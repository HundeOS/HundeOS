#ifndef __KERNEL_PROCESS_CLASS_H
#define __KERNEL_PROCESS_CLASS_H

#include <stdint.h>
#include <stdbool.h>

typedef enum ProcessClass
{
    PROCESS_CLASS_NORMAL = 0,
    PROCESS_CLASS_SERVICE = 1,
    PROCESS_CLASS_REALTIME_SERVICE = 2,
} ProcessClass;

bool ProcessClass__is_service(ProcessClass self);
uint64_t ProcessClass__priority(ProcessClass self);
const char * ProcessClass__to_string(ProcessClass self);

#endif

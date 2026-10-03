#include <stddef.h>

#include "memory.h"
#include "system_info.h"

void SBI__shutdown(void);

void * memcpy(void * destination, const void * source, size_t size)
{
    return Mem__memcpy(destination, source, size);
}

void * memset(void * destination, int value, size_t size)
{
    return Mem__memset(destination, value, size);
}

void CPU__enter_kernel(SystemInfoPtr system_info)
{
    SBI__shutdown();
}

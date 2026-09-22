#include <stddef.h>

#include "system_info.h"

void SBI__shutdown(void);

void * memcpy(void * destination, const void * source, size_t size)
{
    unsigned char * target = destination;
    const unsigned char * input = source;
    for (size_t index = 0; index < size; index++)
    {
        target[index] = input[index];
    }
    return destination;
}

void * memset(void * destination, int value, size_t size)
{
    unsigned char * cursor = destination;
    for (size_t index = 0; index < size; index++)
    {
        cursor[index] = (unsigned char)value;
    }
    return destination;
}

void CPU__enter_kernel(SystemInfoPtr system_info)
{
    SBI__shutdown();
}

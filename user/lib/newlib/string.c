#include <stddef.h>

void * memset(void * dest, int value, size_t size)
{
    unsigned char * cursor = dest;

    for (size_t i = 0; i < size; i++)
    {
        cursor[i] = (unsigned char) value;
    }

    return dest;
}

void * memcpy(void * dest, const void * src, size_t size)
{
    unsigned char * dest_cursor = dest;
    const unsigned char * src_cursor = src;

    for (size_t i = 0; i < size; i++)
    {
        dest_cursor[i] = src_cursor[i];
    }

    return dest;
}

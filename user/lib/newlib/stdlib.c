#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "runtime.h"

static const char * program_name = "program";
char * __progname = "program";
static uint32_t rand_state = 1;

int rand(void)
{
    rand_state = rand_state * UINT32_C(1103515245) + UINT32_C(12345);
    return (int)(rand_state & (uint32_t)RAND_MAX);
}

void srand(unsigned int seed)
{
    rand_state = seed;
}

void LibC__set_program_name(const char * argument_zero)
{
    const char * basename = argument_zero;
    for (const char * cursor = argument_zero; *cursor != '\0'; cursor++)
    {
        if (*cursor == '/')
        {
            basename = cursor + 1;
        }
    }
    program_name = *basename == '\0' ? argument_zero : basename;
    __progname = (char *)program_name;
}

void * reallocarray(void * address, size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count)
    {
        errno = ENOMEM;
        return NULL;
    }
    return realloc(address, count * size);
}

void * recallocarray(void * address, size_t old_count, size_t new_count, size_t size)
{
    void * resized = reallocarray(address, new_count, size);
    if (resized != NULL && new_count > old_count)
    {
        memset((char *)resized + old_count * size, 0, (new_count - old_count) * size);
    }
    return resized;
}

long long strtonum(const char * text, long long minimum, long long maximum, const char ** error)
{
    char * end;
    unsigned long long parsed = strtoull(text, &end, 10);
    if (*text == '\0' || *end != '\0')
    {
        if (error != NULL)
        {
            *error = "invalid";
        }
        errno = EINVAL;
        return 0;
    }
    long long value = (long long)parsed;
    if (value < minimum || value > maximum)
    {
        if (error != NULL)
        {
            *error = value < minimum ? "too small" : "too large";
        }
        errno = EINVAL;
        return 0;
    }
    if (error != NULL)
    {
        *error = NULL;
    }
    return value;
}

void * setmode(const char * mode)
{
    char * end;
    unsigned long long value = strtoull(mode, &end, 8);
    if (*mode == '\0' || *end != '\0' || value > 07777)
    {
        errno = EINVAL;
        return NULL;
    }
    mode_t * parsed_mode = malloc(sizeof(*parsed_mode));
    if (parsed_mode == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    *parsed_mode = (mode_t)value;
    return parsed_mode;
}

mode_t getmode(const void * set, mode_t mode)
{
    return set == NULL ? mode : *(const mode_t *)set;
}

const char * getprogname(void)
{
    return program_name;
}

int atoi(const char * text)
{
    return (int)strtol(text, NULL, 10);
}

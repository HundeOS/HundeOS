#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>

#include "runtime.h"

static const char * program_name = "program";
char * __progname = "program";

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

char * getenv(const char * name)
{
    return NULL;
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

static bool stdlib_is_space(char ch)
{
    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
}

static int stdlib_digit_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'z')
    {
        return 10 + ch - 'a';
    }
    if (ch >= 'A' && ch <= 'Z')
    {
        return 10 + ch - 'A';
    }

    return 36;
}

static int stdlib_detect_base(const char ** cursor, int base)
{
    if (base == 0)
    {
        if ((*cursor)[0] == '0' && ((*cursor)[1] == 'x' || (*cursor)[1] == 'X'))
        {
            *cursor += 2;
            return 16;
        }
        if ((*cursor)[0] == '0')
        {
            return 8;
        }

        return 10;
    }

    if (base == 16 && (*cursor)[0] == '0' && ((*cursor)[1] == 'x' || (*cursor)[1] == 'X'))
    {
        *cursor += 2;
    }

    return base;
}

unsigned long long strtoull(const char * text, char ** endptr, int base)
{
    const char * cursor = text;
    const char * digits_start;
    unsigned long long value = 0;
    bool negative = false;

    while (stdlib_is_space(*cursor))
    {
        cursor++;
    }

    if (*cursor == '-' || *cursor == '+')
    {
        negative = *cursor == '-';
        cursor++;
    }

    base = stdlib_detect_base(&cursor, base);
    digits_start = cursor;

    while (base >= 2 && base <= 36)
    {
        int digit = stdlib_digit_value(*cursor);
        if (digit >= base)
        {
            break;
        }

        value = (value * (unsigned long long) base) + (unsigned long long) digit;
        cursor++;
    }

    if (cursor == digits_start)
    {
        cursor = text;
    }

    if (endptr != NULL)
    {
        *endptr = (char *) cursor;
    }

    if (negative)
    {
        return (unsigned long long) -value;
    }

    return value;
}

long long strtoll(const char * text, char ** endptr, int base)
{
    bool negative = false;
    const char * cursor = text;
    while (stdlib_is_space(*cursor))
    {
        cursor++;
    }
    if (*cursor == '-' || *cursor == '+')
    {
        negative = *cursor == '-';
        cursor++;
    }
    char * end;
    unsigned long long value = strtoull(cursor, &end, base);
    if (endptr != NULL)
    {
        *endptr = end == cursor ? (char *)text : end;
    }
    return negative ? -(long long)value : (long long)value;
}

int atoi(const char * text)
{
    bool negative = false;

    while (stdlib_is_space(*text))
    {
        text++;
    }

    if (*text == '-' || *text == '+')
    {
        negative = *text == '-';
        text++;
    }

    unsigned long long value = strtoull(text, NULL, 10);
    if (negative)
    {
        return -(int) value;
    }

    return (int) value;
}

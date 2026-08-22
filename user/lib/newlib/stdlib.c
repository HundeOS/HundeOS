#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>

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

#include <stdarg.h>

int k_printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    va_end(args);

    int len = 0;
    while (format != 0 && format[len] != 0)
    {
        len++;
    }
    return len;
}

void k_println(const char * str)
{
    if (str == 0)
    {
        return;
    }
}

int k_sprintf_impl(char * str, const char * format, va_list args)
{
    va_list copy;
    va_copy(copy, args);
    va_end(copy);

    str[0] = 0;
    if (format == 0)
    {
        return 0;
    }
    return 0;
}

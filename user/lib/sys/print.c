#include "print.h"

#include <stdarg.h>
#include <stdint.h>
#include <stddef.h>

#include "io.h"

void IO__print(ConstBStringPtr text)
{
    IO__write(STDOUT_FILENO, text);
}

int IO__vprintf(ConstBStringPtr format, va_list args)
{
    BString text = BString__vsprintf(format, args);
    IO__print(&text);
    return (int)BString__length(&text);
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    int count = IO__vprintf(format, args);
    va_end(args);
    return count;
}

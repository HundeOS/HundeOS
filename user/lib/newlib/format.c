#include <errno.h>
#include <stdarg.h>
#include <stddef.h>

#include "../../common/types/bstring.h"

int vsnprintf(char * buffer, size_t size, const char * format, va_list arguments)
{
    if ((buffer == NULL && size != 0) || format == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    BString format_string = BString__new(format);
    BString formatted = BString__vsprintf(&format_string, arguments);
    size_t length = (size_t)BString__length(&formatted);
    if (size > 0)
    {
        BString__copy_to(&formatted, buffer, (StringPos)size);
    }
    return (int)length;
}

int snprintf(char * buffer, size_t size, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int vsprintf(char * buffer, const char * format, va_list arguments)
{
    if (buffer == NULL || format == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    return vsnprintf(buffer, BSTRING_MAX_SIZE, format, arguments);
}

int sprintf(char * buffer, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return result;
}

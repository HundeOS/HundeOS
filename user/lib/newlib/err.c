#include <err.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>

static void Error__print(const char * format, va_list arguments)
{
    int saved_errno = errno;
    fprintf(stderr, "%s", getprogname());
    if (format != NULL)
    {
        fprintf(stderr, ": ");
        vfprintf(stderr, format, arguments);
    }
    fprintf(stderr, ": %s\n", strerror(saved_errno));
}

static void Error__print_without_errno(const char * format, va_list arguments)
{
    fprintf(stderr, "%s", getprogname());
    if (format != NULL)
    {
        fprintf(stderr, ": ");
        vfprintf(stderr, format, arguments);
    }
    fprintf(stderr, "\n");
}

void warn(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    Error__print(format, arguments);
    va_end(arguments);
}

void err(int status, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    Error__print(format, arguments);
    va_end(arguments);
    exit(status);
}

void warnx(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    Error__print_without_errno(format, arguments);
    va_end(arguments);
}

void warnc(int code, const char * format, ...)
{
    int saved_errno = errno;
    errno = code;
    va_list arguments;
    va_start(arguments, format);
    Error__print(format, arguments);
    va_end(arguments);
    errno = saved_errno;
}

void errx(int status, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    Error__print_without_errno(format, arguments);
    va_end(arguments);
    exit(status);
}

void errc(int status, int code, const char * format, ...)
{
    errno = code;
    va_list arguments;
    va_start(arguments, format);
    Error__print(format, arguments);
    va_end(arguments);
    exit(status);
}

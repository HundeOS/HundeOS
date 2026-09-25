#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../../common/types/bstring.h"

struct FILE
{
    int descriptor;
    int error;
    bool owned;
};

static FILE standard_input = {.descriptor = STDIN_FILENO};
static FILE standard_output = {.descriptor = STDOUT_FILENO};
static FILE standard_error = {.descriptor = STDERR_FILENO};

FILE * stdin = &standard_input;
FILE * stdout = &standard_output;
FILE * stderr = &standard_error;

FILE * fopen(const char * path, const char * mode)
{
    if (mode == NULL || strcmp(mode, "r") != 0)
    {
        errno = EINVAL;
        return NULL;
    }

    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0)
    {
        return NULL;
    }

    FILE * stream = malloc(sizeof(FILE));
    if (stream == NULL)
    {
        close(descriptor);
        errno = ENOMEM;
        return NULL;
    }
    *stream = (FILE){
        .descriptor = descriptor,
        .owned = true,
    };
    return stream;
}

int fclose(FILE * stream)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return EOF;
    }

    int result = close(stream->descriptor);
    if (result != 0)
    {
        stream->error = errno;
    }
    if (stream->owned)
    {
        free(stream);
    }
    return result;
}

int getc(FILE * stream)
{
    unsigned char character;
    ssize_t result = read(stream->descriptor, &character, 1);
    if (result == 1)
    {
        return character;
    }
    if (result < 0)
    {
        stream->error = errno;
    }
    return EOF;
}

int getchar(void)
{
    return getc(stdin);
}

int putchar(int character)
{
    unsigned char output = (unsigned char)character;
    ssize_t result = write(STDOUT_FILENO, &output, 1);
    if (result != 1)
    {
        stdout->error = errno;
        return EOF;
    }
    return output;
}

int fileno(FILE * stream)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    return stream->descriptor;
}

int ferror(FILE * stream)
{
    return stream == NULL ? 0 : stream->error;
}

void clearerr(FILE * stream)
{
    if (stream != NULL)
    {
        stream->error = 0;
    }
}

int fflush(FILE * stream)
{
    return 0;
}

int setvbuf(FILE * stream, char * buffer, int mode, size_t size)
{
    if (stream == NULL || mode != _IONBF)
    {
        errno = EINVAL;
        return -1;
    }
    return 0;
}

int vfprintf(FILE * stream, const char * format, va_list arguments)
{
    if (stream == NULL || format == NULL)
    {
        errno = EINVAL;
        return -1;
    }

    BString format_string = BString__new(format);
    BString output = BString__vsprintf(&format_string, arguments);
    size_t written = 0;
    while (written < (size_t)BString__length(&output))
    {
        ssize_t result = write(stream->descriptor, BString__cstr(&output) + written,
                               (size_t)BString__length(&output) - written);
        if (result <= 0)
        {
            stream->error = errno;
            return -1;
        }
        written += (size_t)result;
    }
    return (int)written;
}

int fprintf(FILE * stream, const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int printf(const char * format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}

int snprintf(char * buffer, size_t size, const char * format, ...)
{
    if ((buffer == NULL && size != 0) || format == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    va_list arguments;
    va_start(arguments, format);
    BString format_string = BString__new(format);
    BString formatted = BString__vsprintf(&format_string, arguments);
    va_end(arguments);
    size_t length = (size_t)BString__length(&formatted);
    if (size > 0)
    {
        BString__copy_to(&formatted, buffer, (StringPos)size);
    }
    return (int)length;
}

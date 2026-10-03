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
    int pushed_back;
    off_t read_buffer_offset;
    size_t read_buffer_length;
    size_t read_buffer_position;
    size_t write_buffer_length;
    unsigned char read_buffer[BUFSIZ];
    unsigned char write_buffer[BUFSIZ];
    int buffer_mode;
    bool eof;
    bool has_pushed_back;
    bool owned;
    bool seekable;
};

static FILE standard_input = {.descriptor = STDIN_FILENO};
static FILE standard_output = {.descriptor = STDOUT_FILENO, .buffer_mode = _IOLBF};
static FILE standard_error = {.descriptor = STDERR_FILENO};

FILE * stdin = &standard_input;
FILE * stdout = &standard_output;
FILE * stderr = &standard_error;

static off_t StdioFile__position(FILE * self)
{
    off_t position = self->read_buffer_offset + (off_t)self->read_buffer_position;
    return self->has_pushed_back && position > 0 ? position - 1 : position;
}

static void StdioFile__reset_read_buffer(FILE * self, off_t position)
{
    self->read_buffer_offset = position;
    self->read_buffer_length = 0;
    self->read_buffer_position = 0;
    self->has_pushed_back = false;
    self->eof = false;
}

static int StdioFile__write_all(FILE * self, const unsigned char * data, size_t length)
{
    size_t written = 0;
    while (written < length)
    {
        ssize_t result = write(self->descriptor, data + written, length - written);
        if (result <= 0)
        {
            self->error = errno;
            return EOF;
        }
        written += (size_t)result;
    }
    return 0;
}

static int StdioFile__flush_write_buffer(FILE * self)
{
    if (self->write_buffer_length == 0)
    {
        return 0;
    }
    size_t written = 0;
    while (written < self->write_buffer_length)
    {
        ssize_t result = write(self->descriptor, self->write_buffer + written,
                               self->write_buffer_length - written);
        if (result <= 0)
        {
            self->error = errno;
            if (written > 0)
            {
                memmove(self->write_buffer, self->write_buffer + written,
                        self->write_buffer_length - written);
                self->write_buffer_length -= written;
            }
            return EOF;
        }
        written += (size_t)result;
    }
    self->write_buffer_length = 0;
    return 0;
}

static bool StdioFile__buffer_contains(FILE * self, off_t position)
{
    return position >= self->read_buffer_offset &&
           position <= self->read_buffer_offset + (off_t)self->read_buffer_length;
}

static int StdioFile__fill_at(FILE * self, off_t position)
{
    off_t window = position - (position % BUFSIZ);
    if (lseek(self->descriptor, window, SEEK_SET) < 0)
    {
        self->error = errno;
        return -1;
    }
    ssize_t length = read(self->descriptor, self->read_buffer, sizeof(self->read_buffer));
    if (length < 0)
    {
        self->error = errno;
        return -1;
    }
    self->read_buffer_offset = window;
    self->read_buffer_length = (size_t)length;
    if (!StdioFile__buffer_contains(self, position))
    {
        if (lseek(self->descriptor, position, SEEK_SET) < 0)
        {
            self->error = errno;
            return -1;
        }
        StdioFile__reset_read_buffer(self, position);
        return 0;
    }
    self->read_buffer_position = (size_t)(position - window);
    return 0;
}

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
        .seekable = true,
    };
    return stream;
}

FILE * fdopen(int descriptor, const char * mode)
{
    if (descriptor < 0 || mode == NULL || strcmp(mode, "r") != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    FILE * stream = malloc(sizeof(FILE));
    if (stream == NULL)
    {
        errno = ENOMEM;
        return NULL;
    }
    *stream = (FILE){.descriptor = descriptor, .owned = true, .seekable = true};
    return stream;
}

int fclose(FILE * stream)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return EOF;
    }

    int result = StdioFile__flush_write_buffer(stream);
    int close_result = close(stream->descriptor);
    if (result == 0)
    {
        result = close_result;
    }
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
    if (stream == NULL)
    {
        errno = EINVAL;
        return EOF;
    }
    if (stream->has_pushed_back)
    {
        stream->has_pushed_back = false;
        stream->eof = false;
        return stream->pushed_back;
    }
    if (stream->seekable)
    {
        if (stream->read_buffer_position == stream->read_buffer_length)
        {
            off_t position = stream->read_buffer_offset + (off_t)stream->read_buffer_length;
            ssize_t length = read(stream->descriptor, stream->read_buffer,
                                  sizeof(stream->read_buffer));
            if (length < 0)
            {
                stream->error = errno;
                return EOF;
            }
            stream->read_buffer_offset = position;
            stream->read_buffer_length = (size_t)length;
            stream->read_buffer_position = 0;
            if (length == 0)
            {
                stream->eof = true;
                return EOF;
            }
        }
        stream->eof = false;
        return stream->read_buffer[stream->read_buffer_position++];
    }

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
    else
    {
        stream->eof = true;
    }
    return EOF;
}

int ungetc(int character, FILE * stream)
{
    if (stream == NULL || character == EOF || stream->has_pushed_back)
    {
        return EOF;
    }
    stream->pushed_back = (unsigned char)character;
    stream->has_pushed_back = true;
    stream->eof = false;
    return stream->pushed_back;
}

int getchar(void)
{
    return getc(stdin);
}

int putc(int character, FILE * stream)
{
    unsigned char output = (unsigned char)character;
    return fwrite(&output, 1, 1, stream) == 1 ? output : EOF;
}

int putchar(int character)
{
    return putc(character, stdout);
}

int fputs(const char * text, FILE * stream)
{
    if (text == NULL || stream == NULL)
    {
        errno = EINVAL;
        return EOF;
    }
    size_t length = strlen(text);
    return fwrite(text, 1, length, stream) == length ? 0 : EOF;
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
        stream->eof = false;
    }
}

int feof(FILE * stream)
{
    return stream != NULL && stream->eof;
}

int fpurge(FILE * stream)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return EOF;
    }
    if (stream->seekable)
    {
        off_t position = StdioFile__position(stream);
        if (lseek(stream->descriptor, position, SEEK_SET) < 0)
        {
            stream->error = errno;
            return EOF;
        }
        StdioFile__reset_read_buffer(stream, position);
    }
    else
    {
        stream->has_pushed_back = false;
        stream->eof = false;
    }
    stream->write_buffer_length = 0;
    return 0;
}

int fseeko(FILE * stream, off_t offset, int whence)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (!stream->seekable)
    {
        off_t result = lseek(stream->descriptor, offset, whence);
        if (result < 0)
        {
            stream->error = errno;
            return -1;
        }
        return 0;
    }

    off_t base;
    if (whence == SEEK_SET)
    {
        base = 0;
    }
    else if (whence == SEEK_CUR)
    {
        base = StdioFile__position(stream);
    }
    else if (whence == SEEK_END)
    {
        base = lseek(stream->descriptor, 0, SEEK_END);
        if (base < 0)
        {
            stream->error = errno;
            return -1;
        }
    }
    else
    {
        errno = EINVAL;
        stream->error = errno;
        return -1;
    }

    off_t position;
    if (__builtin_add_overflow(base, offset, &position) || position < 0)
    {
        errno = EINVAL;
        stream->error = errno;
        return -1;
    }
    stream->has_pushed_back = false;
    stream->eof = false;
    if (StdioFile__buffer_contains(stream, position))
    {
        stream->read_buffer_position = (size_t)(position - stream->read_buffer_offset);
        return 0;
    }
    return StdioFile__fill_at(stream, position);
}

int fseek(FILE * stream, long offset, int whence)
{
    return fseeko(stream, (off_t)offset, whence);
}

off_t ftello(FILE * stream)
{
    if (stream == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    return stream->seekable ? StdioFile__position(stream)
                            : lseek(stream->descriptor, 0, SEEK_CUR);
}

void rewind(FILE * stream)
{
    if (stream != NULL)
    {
        clearerr(stream);
        fseeko(stream, 0, SEEK_SET);
    }
}

FILE * freopen(const char * path, const char * mode, FILE * stream)
{
    if (stream == NULL || mode == NULL || strcmp(mode, "r") != 0)
    {
        errno = EINVAL;
        return NULL;
    }
    int descriptor = open(path, O_RDONLY);
    if (descriptor < 0)
    {
        return NULL;
    }
    close(stream->descriptor);
    stream->descriptor = descriptor;
    stream->error = 0;
    stream->seekable = true;
    stream->write_buffer_length = 0;
    StdioFile__reset_read_buffer(stream, 0);
    return stream;
}

size_t fread(void * buffer, size_t size, size_t count, FILE * stream)
{
    if (stream == NULL || (size != 0 && count > SIZE_MAX / size))
    {
        errno = EINVAL;
        return 0;
    }
    size_t requested = size * count;
    if (buffer == NULL && requested != 0)
    {
        errno = EINVAL;
        return 0;
    }
    size_t bytes = 0;
    while (bytes < requested)
    {
        int character = getc(stream);
        if (character == EOF)
        {
            break;
        }
        ((unsigned char *)buffer)[bytes++] = (unsigned char)character;
    }
    return size == 0 ? 0 : bytes / size;
}

size_t fwrite(const void * buffer, size_t size, size_t count, FILE * stream)
{
    if (stream == NULL || (size != 0 && count > SIZE_MAX / size))
    {
        errno = EINVAL;
        return 0;
    }
    size_t requested = size * count;
    if (buffer == NULL && requested != 0)
    {
        errno = EINVAL;
        return 0;
    }
    if (requested == 0)
    {
        return 0;
    }
    if (stream->buffer_mode != _IOLBF)
    {
        return StdioFile__write_all(stream, buffer, requested) == 0 ? count : 0;
    }

    const unsigned char * input = buffer;
    size_t accepted = 0;
    while (accepted < requested)
    {
        unsigned char character = input[accepted++];
        stream->write_buffer[stream->write_buffer_length++] = character;
        if (character == '\n' || stream->write_buffer_length == sizeof(stream->write_buffer))
        {
            if (StdioFile__flush_write_buffer(stream) == EOF)
            {
                return (accepted - 1) / size;
            }
        }
    }
    return count;
}

ssize_t getline(char ** line, size_t * capacity, FILE * stream)
{
    if (line == NULL || capacity == NULL || stream == NULL)
    {
        errno = EINVAL;
        return -1;
    }
    if (*line == NULL || *capacity == 0)
    {
        *capacity = 128;
        *line = malloc(*capacity);
        if (*line == NULL)
        {
            errno = ENOMEM;
            return -1;
        }
    }
    size_t length = 0;
    int character;
    while ((character = getc(stream)) != EOF)
    {
        if (length + 1 >= *capacity)
        {
            size_t grown = *capacity > SIZE_MAX / 2 ? SIZE_MAX : *capacity * 2;
            char * resized = realloc(*line, grown);
            if (resized == NULL)
            {
                errno = ENOMEM;
                return -1;
            }
            *line = resized;
            *capacity = grown;
        }
        (*line)[length++] = (char)character;
        if (character == '\n')
        {
            break;
        }
    }
    if (length == 0)
    {
        return -1;
    }
    (*line)[length] = '\0';
    return (ssize_t)length;
}

int fflush(FILE * stream)
{
    if (stream == NULL)
    {
        int output_result = StdioFile__flush_write_buffer(stdout);
        int error_result = StdioFile__flush_write_buffer(stderr);
        return output_result == 0 && error_result == 0 ? 0 : EOF;
    }
    return StdioFile__flush_write_buffer(stream);
}

int setvbuf(FILE * stream, char * buffer, int mode, size_t size)
{
    if (stream == NULL || (mode != _IONBF && mode != _IOLBF))
    {
        errno = EINVAL;
        return -1;
    }
    if (StdioFile__flush_write_buffer(stream) == EOF)
    {
        return -1;
    }
    stream->buffer_mode = mode;
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
    size_t length = (size_t)BString__length(&output);
    return fwrite(BString__cstr(&output), 1, length, stream) == length ? (int)length : -1;
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

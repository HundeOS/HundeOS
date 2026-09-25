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

void * memmove(void * dest, const void * src, size_t size)
{
    unsigned char * dest_cursor = dest;
    const unsigned char * src_cursor = src;
    if (dest_cursor < src_cursor)
    {
        return memcpy(dest, src, size);
    }
    while (size > 0)
    {
        size--;
        dest_cursor[size] = src_cursor[size];
    }
    return dest;
}

size_t strlen(const char * text)
{
    size_t length = 0;
    while (text[length] != '\0')
    {
        length++;
    }
    return length;
}

int strcmp(const char * left, const char * right)
{
    while (*left != '\0' && *left == *right)
    {
        left++;
        right++;
    }
    return (unsigned char)*left - (unsigned char)*right;
}

int strncmp(const char * left, const char * right, size_t size)
{
    for (size_t index = 0; index < size; index++)
    {
        unsigned char left_char = (unsigned char)left[index];
        unsigned char right_char = (unsigned char)right[index];
        if (left_char != right_char || left_char == '\0')
        {
            return left_char - right_char;
        }
    }
    return 0;
}

int memcmp(const void * left, const void * right, size_t size)
{
    const unsigned char * left_bytes = left;
    const unsigned char * right_bytes = right;
    for (size_t index = 0; index < size; index++)
    {
        if (left_bytes[index] != right_bytes[index])
        {
            return left_bytes[index] - right_bytes[index];
        }
    }
    return 0;
}

char * strcpy(char * dest, const char * source)
{
    size_t index = 0;
    do
    {
        dest[index] = source[index];
    } while (source[index++] != '\0');
    return dest;
}

char * strncat(char * dest, const char * source, size_t size)
{
    size_t dest_length = strlen(dest);
    size_t index = 0;
    while (index < size && source[index] != '\0')
    {
        dest[dest_length + index] = source[index];
        index++;
    }
    dest[dest_length + index] = '\0';
    return dest;
}

char * strchr(const char * text, int character)
{
    char target = (char)character;
    do
    {
        if (*text == target)
        {
            return (char *)text;
        }
    } while (*text++ != '\0');
    return NULL;
}

char * strrchr(const char * text, int character)
{
    const char * match = NULL;
    char target = (char)character;
    do
    {
        if (*text == target)
        {
            match = text;
        }
    } while (*text++ != '\0');
    return (char *)match;
}

static int String__contains(const char * characters, char target)
{
    while (*characters != '\0')
    {
        if (*characters++ == target)
        {
            return 1;
        }
    }
    return 0;
}

size_t strspn(const char * text, const char * accept)
{
    size_t length = 0;
    while (text[length] != '\0' && String__contains(accept, text[length]))
    {
        length++;
    }
    return length;
}

size_t strcspn(const char * text, const char * reject)
{
    size_t length = 0;
    while (text[length] != '\0' && !String__contains(reject, text[length]))
    {
        length++;
    }
    return length;
}

size_t strlcpy(char * dest, const char * source, size_t size)
{
    size_t source_length = strlen(source);
    if (size > 0)
    {
        size_t copy_length = source_length < size - 1 ? source_length : size - 1;
        memcpy(dest, source, copy_length);
        dest[copy_length] = '\0';
    }
    return source_length;
}

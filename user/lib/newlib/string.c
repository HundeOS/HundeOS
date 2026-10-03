#include <stddef.h>
#include <stdlib.h>
#include <string.h>

static unsigned char String__ascii_lower(unsigned char character)
{
    if (character >= 'A' && character <= 'Z')
    {
        return (unsigned char)(character + ('a' - 'A'));
    }
    return character;
}

int strcasecmp(const char * left, const char * right)
{
    while (*left != '\0' && String__ascii_lower((unsigned char)*left) == String__ascii_lower((unsigned char)*right))
    {
        left++;
        right++;
    }
    return String__ascii_lower((unsigned char)*left) - String__ascii_lower((unsigned char)*right);
}

int strncasecmp(const char * left, const char * right, size_t size)
{
    for (size_t index = 0; index < size; index++)
    {
        unsigned char left_char = String__ascii_lower((unsigned char)left[index]);
        unsigned char right_char = String__ascii_lower((unsigned char)right[index]);
        if (left_char != right_char || left_char == '\0')
        {
            return left_char - right_char;
        }
    }
    return 0;
}

char * strdup(const char * source)
{
    size_t size = strlen(source) + 1;
    char * copy = malloc(size);
    if (copy == NULL)
    {
        return NULL;
    }
    return memcpy(copy, source, size);
}

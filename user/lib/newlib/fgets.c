#include <errno.h>
#include <stdio.h>

char * fgets(char * text, int size, FILE * stream)
{
    if (text == NULL || size <= 0 || stream == NULL)
    {
        errno = EINVAL;
        return NULL;
    }

    int length = 0;
    while (length + 1 < size)
    {
        int character = getc(stream);
        if (character == EOF)
        {
            break;
        }
        text[length++] = (char)character;
        if (character == '\n')
        {
            break;
        }
    }
    if (length == 0)
    {
        return NULL;
    }
    text[length] = '\0';
    return text;
}

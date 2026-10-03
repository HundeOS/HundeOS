#include <stdio.h>

int puts(const char * text)
{
    if (fputs(text, stdout) == EOF)
    {
        return EOF;
    }
    return putchar('\n') == EOF ? EOF : 0;
}

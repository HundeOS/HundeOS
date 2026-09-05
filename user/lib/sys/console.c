#include <stdint.h>
#include <stddef.h>

#include "../../common/types/bstring.h"
#include "io.h"
#include "print.h"
#include "../../user.h"

enum
{
    BUFFER_SIZE = 1024
};

ByteCount console_read(char * buf, uint64_t size)
{
    ByteCount res = 0;
    res = read(STDIN_FILENO, buf, size);

    if (res > 0 && res < (ByteCount) size)
    {
        buf[res] = '\0';
    }
    return res;
}

ByteCount console_read_line(char * buf, bool echo, uint64_t size)
{
    if (size == 0)
    {
        return 0;
    }

    size_t pos = 0;
    size_t max_pos = size - 1;

    while (pos < max_pos)
    {
        char input[BUFFER_SIZE];
        ByteCount read = console_read(input, BUFFER_SIZE - 1);

        for (ByteCount ch = 0; ch < read && pos < max_pos; ch++)
        {
            char c = input[ch];

            if (c == '\b' || c == 0x7f)
            {
                if (pos > 0)
                {
                    pos--;
                    buf[pos] = '\0';
                    if (echo)
                    {
                        IO__print(&bstr(" "));
                    }
                }
                continue;
            }

            if (c == '\n' || c == '\r')
            {
                buf[pos] = '\0';
                return pos;
            }

            buf[pos++] = c;
            buf[pos] = '\0';

            if (echo)
            {
                BString output = BString__from_chars(&c, 1);
                IO__print(&output);
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

BString Console__read(void)
{
    char buffer[BSTRING_MAX_SIZE];
    ByteCount read = console_read(buffer, BSTRING_MAX_SIZE - 1);

    if (read <= 0)
    {
        return empty_bstr();
    }

    return BString__from_chars(buffer, read);
}

BString Console__read_line(bool echo)
{
    char buffer[BSTRING_MAX_SIZE];
    ByteCount read = console_read_line(buffer, echo, BSTRING_MAX_SIZE);

    if (read <= 0)
    {
        return empty_bstr();
    }

    return BString__from_chars(buffer, read);
}

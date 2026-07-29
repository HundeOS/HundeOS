#include <stdint.h>
#include <stddef.h>

#include "../../common/types/bstring.h"
#include "io.h"
#include "../user.h"

extern void print(const char * str);

enum
{
    BUFFER_SIZE = 1024
};
static const char backspace_sequence[] = "\b \b";

ByteCount console_read(char * buf, uint64_t size)
{
    ByteCount res = 0;
    res = read(STDIN_FILENO, buf, size);

    if (res > 0)
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
                        print(backspace_sequence);
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
                char out[2] = { c, '\0' };
                print(out);
            }
        }
    }

    buf[pos] = '\0';
    return pos;
}

BString Console__read(void)
{
    BString input = EMPTY_BSTRING;
    ByteCount read = console_read(input.data, input.max - 1);

    if (read <= 0)
    {
        return input;
    }

    input.len = read;
    input.data[input.len] = '\0';
    return input;
}

BString Console__read_line(bool echo)
{
    BString input = EMPTY_BSTRING;
    ByteCount read = console_read_line(input.data, echo, input.max);

    if (read <= 0)
    {
        return input;
    }

    input.len = read;
    input.data[input.len] = '\0';
    return input;
}

#include <stdint.h>
#include <stddef.h>

#include "io.h"
#include "exit.h"
#include "../user.h"

extern void print(const char * str);

static IOHandle console_handle = 0;

const size_t BUFFER_SIZE = 1024;
static const char backspace_sequence[] = "\b \b";

ByteCount console_input(char * buf, size_t size)
{
    if (console_handle == 0)
    {
        console_handle = open(":console:", 0);

        if (console_handle <= 0)
        {
            print("invalid handle\n");
            exit(-1);
        }
    }

    ByteCount res = 0;
    res = read(console_handle, buf, size);

    if (res > 0)
    {
        buf[res] = '\0';
    }
    return res;
}

ByteCount console_read_line(char * buf, bool echo, size_t size)
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
        ByteCount read = console_input(input, BUFFER_SIZE - 1);

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

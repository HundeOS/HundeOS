#include "syscall.h"
#include <stdint.h>
#include <stddef.h>

#include "io.h"
#include "exit.h"
#include "../../user.h"

extern void print(const char * str);

static IOHandle console_handle = 0;

const size_t BUFFER_SIZE = 1024;

ByteCount console_input(char * buf, size_t size)
{
    if (console_handle == 0)
    {
        console_handle = open("/dev/console", 0);

        if (console_handle <= 0)
        {
            print("invalid handle\n");
            exit(-1);
        }
    }

    ByteCount res = 0;
    while (res == 0)
    {
        res = read(console_handle, buf, size);
        asm volatile("wfi");
    }

    if (res > 0)
    {
        buf[res] = '\0';
    }
    return res;
}

ByteCount console_read_line(char * buf, bool echo, size_t size)
{
    ByteCount read = 0;
    size_t size_with_term_space = size - 1;
    for (int i = 0; i < size_with_term_space; i+= read)
    {
        read = console_input(&buf[i], size);
        if (echo)
        {
            for (int ch = 0; ch < read; ch++)
            {
                print(&buf[i + ch]);
            }
        }
        if (buf[i] == '\n' || buf[i] == '\r')
        {
            buf[i] = '\0';
            break;
        }
    }
    return read;
}

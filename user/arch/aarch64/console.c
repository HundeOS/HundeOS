#include "syscall.h"
#include <stdint.h>
#include <stddef.h>

#include "io.h"
#include "exit.h"

extern void print(const char * str);

static IOHandle console_handle = 0;

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

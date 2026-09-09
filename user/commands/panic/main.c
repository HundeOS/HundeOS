#include "../../user.h"
#include "sys/system.h"

#ifndef PANIC_ENTRYPOINT
#define PANIC_ENTRYPOINT main
#endif

int PANIC_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != 1)
    {
        IO__print(&bstr("panic: usage panic\n"));
        return 1;
    }

    SystemCallResult result = panic_system();
    IO__printf(&bstr("panic: syscall returned %i\n"), (int)result);
    return 1;
}

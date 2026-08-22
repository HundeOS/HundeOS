#include "../../user.h"
#include "../../sys/system.h"

#ifndef PANIC_ENTRYPOINT
#define PANIC_ENTRYPOINT main
#endif

int PANIC_ENTRYPOINT(int argc, char ** argv)
{
    if (argc != 1)
    {
        print("panic: usage panic\n");
        return 1;
    }

    SystemCallResult result = panic_system();
    printf("panic: syscall returned %i\n", (int) result);
    return 1;
}

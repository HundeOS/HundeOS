#include "../../../user.h"
#include "../../sys/process.h"

extern int main(int argc, char ** argv);

void _start(int argc, char ** argv, uint64_t pid)
{
    process_set_startup_pid(pid);
    int code = main(argc, argv);
    exit(code);

    for (;;)
    {
    }
}

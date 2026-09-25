#include "../../../user.h"
#include "../../sys/process.h"
#include "../../sys/io.h"
#include "../../newlib/runtime.h"

extern int main(int argc, char ** argv);

void _start(int argc, char ** argv, uint64_t pid, IOHandle input, IOHandle output, IOHandle error)
{
    process_set_startup_pid(pid);
    IO__set_startup_stdio(input, output, error);
    LibC__set_program_name(argv[0]);
    int code = main(argc, argv);
    exit(code);

    for (;;)
    {
    }
}

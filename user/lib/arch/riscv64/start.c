#include "../../../user.h"
#include "../../sys/io.h"
#include "../../sys/process.h"
#include "../../newlib/posix_io.h"
#include "../../newlib/runtime.h"

extern int main(int argc, char ** argv);

void _start(int argc, char ** argv, char ** envp, uint64_t pid, IOHandle input, IOHandle output, IOHandle error,
            ConstExecPayloadPtr exec_payload)
{
    process_set_startup_pid(pid);
    IO__set_startup_stdio(input, output, error);
    LibC__set_environment(envp);
    LibC__set_current_directory("/");
    if (ExecPayload__is_present(exec_payload))
    {
        LibC__set_current_directory(ExecPayload__current_directory(exec_payload));
        if (PosixIO__claim_exec(exec_payload) != IO_OK)
        {
            exit(127);
        }
    }
    LibC__set_program_name(argv[0]);
    int code = main(argc, argv);
    exit(code);

    for (;;)
    {
    }
}

#include "interpreter.h"

#include "../../lib/newlib/current_directory.h"
#include "../../lib/newlib/runtime.h"
#include "../../lib/sys/io.h"
#include "../../lib/sys/process.h"
#include "../../lib/sys/syslog.h"
#include "../../user.h"
#include "syscalls/exec.h"

typedef void (*WuffProgramEntry)(int argc, char ** argv, char ** environment_argv, uint64_t pid, IOHandle input,
                                 IOHandle output, IOHandle error, ConstExecPayloadPtr exec_payload);
OptionalType(const char *, ConstCharPtr);

static void WuffInterpreter__transfer(WuffInterpreterProgramPtr program, int argc, char ** argv,
                                      char ** environment_argv, uint64_t pid, IOHandle input, IOHandle output,
                                      IOHandle error, ConstExecPayloadPtr exec_payload)
{
    WuffProgramEntry entry = (WuffProgramEntry)(uintptr_t)WuffInterpreterProgram__entry(program);
    entry(argc, argv, environment_argv, pid, input, output, error, exec_payload);
}

void _start(int argc, char ** argv, char ** environment_argv, uint64_t pid, IOHandle input, IOHandle output,
            IOHandle error, ConstExecPayloadPtr exec_payload)
{
    process_set_startup_pid(pid);
    IO__set_startup_stdio(input, output, error);
    LibC__set_environment(environment_argv);
    LibC__set_current_directory("/");
    LibC__set_program_name(WUFF_INTERPRETER_PATH);

    Optional(ConstCharPtr) executable_path = argc > 0 ? Some(ConstCharPtr, argv[0]) : None(ConstCharPtr);
    if (ExecPayload__is_present(exec_payload))
    {
        LibC__set_current_directory(ExecPayload__current_directory(exec_payload));
        executable_path = Some(ConstCharPtr, ExecPayload__path(exec_payload));
    }
    BString path;
    match_optional(
        executable_path,
        some(value)
        {
            if (PosixPath__absolute(value, &path) != 0)
            {
                exit(127);
            }
        },
        none()
        {
            exit(127);
        }
    );

    match_result(
        WuffInterpreter__load(&path),
        success(program)
        {
            WuffInterpreter__transfer(&program, argc, argv, environment_argv, pid, input, output, error, exec_payload);
            kernel_syslog_tagf("wuff-ld", "dynamic entry returned unexpectedly");
            match_result(
                WuffGraph__dispose(WuffInterpreterProgram__graph(&program)),
                success_do_nothing(),
                error(code, message)
                {
                    kernel_syslog_tagf("wuff-ld", "cleanup failed: %B (%i)", &message, (int)code);
                    exit(127);
                }
            );
        },
        error(code, message)
        {
            kernel_syslog_tagf("wuff-ld", "load failed for %B: %B (%i)", &path, &message, (int)code);
            exit(127);
        }
    );
    exit(127);

    for (;;)
    {
    }
}

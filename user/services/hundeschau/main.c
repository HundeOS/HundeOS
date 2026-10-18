#include "sys/spawn.h"
#include "sys/framebuffer_config.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/service.h"
#include "types/fixed_array.h"
#include "../../user.h"

#ifndef HUNDESCHAU_ENTRYPOINT
#define HUNDESCHAU_ENTRYPOINT main
#endif

#define HUNDESCHAU_REQUIRED_PROCESSES_MAX 16U
#define HUNDESCHAU_CONFIG_PATH "/boot/config/hundeschau.conf"
#define HUNDESCHAU_CONFIG_MAX 2048U
#define HUNDESCHAU_TERMINAL_INPUT ":tty:"
#define HUNDESCHAU_KEYBOARD_INPUT ":keyboard:"
#define HUNDESCHAU_TERMINAL_OUTPUT ":console:"

static BString hundeschau_empty = empty_bstr();
static BString hundeschau_login_path = bstr("/sbin/login");
static BString hundeschau_humpen_path = bstr("/sbin/services/humpen");
static BString hundeschau_video_term_path = bstr("/sbin/drivers/video_term");
static BString hundeschau_terminal_input = bstr(HUNDESCHAU_TERMINAL_INPUT);
static BString hundeschau_keyboard_input = bstr(HUNDESCHAU_KEYBOARD_INPUT);
static BString hundeschau_terminal_output = bstr(HUNDESCHAU_TERMINAL_OUTPUT);
static BString hundeschau_space_delimiters = bstr(" \t\r");
static BString hundeschau_comment_delimiter = bstr("#");
static BString hundeschau_driver_command = bstr("driver");
static BString hundeschau_once_option = bstr("once");
static BString hundeschau_respawn_option = bstr("respawn");
static BString hundeschau_framebuffer_condition = bstr("framebuffer");
static BString hundeschau_no_framebuffer_condition = bstr("no-framebuffer");
static BString hundeschau_always_condition = bstr("always");

typedef struct HundeschauRequiredProcess
{
    uint64_t pid;
    bool respawn;
    bool has_stdio;
    SpawnFileDescriptor stdin_fd;
    SpawnFileDescriptor stdout_fd;
    SpawnFileDescriptor stderr_fd;
    BString path;
    BString arg0;
    BString arg1;
} HundeschauRequiredProcess;

typedef HundeschauRequiredProcess * HundeschauRequiredProcessPtr;
typedef const HundeschauRequiredProcess * ConstHundeschauRequiredProcessPtr;

OptionalDef(HundeschauRequiredProcess);
OptionalDef(HundeschauRequiredProcessPtr);

static HundeschauRequiredProcess HundeschauRequiredProcess__new(uint64_t pid, bool respawn, ConstBStringPtr path,
                                                                ConstBStringPtr arg0, ConstBStringPtr arg1)
{
    return (HundeschauRequiredProcess){
        .pid = pid,
        .respawn = respawn,
        .has_stdio = false,
        .path = *path,
        .arg0 = *arg0,
        .arg1 = *arg1,
    };
}

static HundeschauRequiredProcess HundeschauRequiredProcess__new_with_stdio(uint64_t pid, bool respawn,
                                                                           SpawnFileDescriptor stdin_fd,
                                                                           SpawnFileDescriptor stdout_fd,
                                                                           SpawnFileDescriptor stderr_fd,
                                                                           ConstBStringPtr path)
{
    return (HundeschauRequiredProcess){
        .pid = pid,
        .respawn = respawn,
        .has_stdio = true,
        .stdin_fd = stdin_fd,
        .stdout_fd = stdout_fd,
        .stderr_fd = stderr_fd,
        .path = *path,
        .arg0 = empty_bstr(),
        .arg1 = empty_bstr(),
    };
}

static bool HundeschauRequiredProcess__equals(HundeschauRequiredProcessPtr self, HundeschauRequiredProcessPtr other)
{
    return self->pid == other->pid;
}

static bool HundeschauRequiredProcess__equals_fixed_array(void * self, void * other)
{
    return HundeschauRequiredProcess__equals((HundeschauRequiredProcessPtr)self, (HundeschauRequiredProcessPtr)other);
}

FixedArrayDefine(HundeschauRequiredProcess, HUNDESCHAU_REQUIRED_PROCESSES_MAX, HundeschauRequiredProcesses,
                 &HundeschauRequiredProcess__equals_fixed_array);
FixedArrayBodyDefine(HundeschauRequiredProcesses);

typedef HundeschauRequiredProcesses * HundeschauRequiredProcessesPtr;

static int Hundeschau__spawn_required_process(HundeschauRequiredProcessesPtr processes, ConstBStringPtr path,
                                              ConstBStringPtr arg0, ConstBStringPtr arg1, bool respawn)
{
    Optional(BString) process_arg0 = SpawnArg__from_bstring(arg0);
    Optional(BString) process_arg1 = SpawnArg__from_bstring(arg1);
    SpawnResult result = spawn(P_NOWAIT, path, &process_arg0, &process_arg1, &SPAWN_NO_ARG);
    if (result < 0)
    {
        IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), path, (int)result);
        return 1;
    }

    HundeschauRequiredProcess process = HundeschauRequiredProcess__new((uint64_t)result, respawn, path, arg0, arg1);
    match_result(
        HundeschauRequiredProcesses__push(processes, process),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("hundeschau: failed to track process: %B\n"), &message);
            return 1;
        }
    );

    return 0;
}

static int Hundeschau__spawn_required_process_with_stdio(HundeschauRequiredProcessesPtr processes, ConstBStringPtr path,
                                                         SpawnFileDescriptor stdin_fd, SpawnFileDescriptor stdout_fd,
                                                         SpawnFileDescriptor stderr_fd, bool respawn)
{
    SpawnResult result =
        spawn_with_stdio(P_NOWAIT, path, stdin_fd, stdout_fd, stderr_fd, &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG);
    if (result < 0)
    {
        IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), path, (int)result);
        return 1;
    }

    HundeschauRequiredProcess process =
        HundeschauRequiredProcess__new_with_stdio((uint64_t)result, respawn, stdin_fd, stdout_fd, stderr_fd, path);
    match_result(
        HundeschauRequiredProcesses__push(processes, process),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("hundeschau: failed to track process: %B\n"), &message);
            return 1;
        }
    );

    return 0;
}

static int Hundeschau__spawn_login(HundeschauRequiredProcessesPtr processes, ConstBStringPtr input_path)
{
    SpawnFileDescriptor input =
        BString__equals(input_path, &hundeschau_keyboard_input) ? IO_ENDPOINT_KEYBOARD_INPUT : IO_ENDPOINT_TTY_INPUT;
    return Hundeschau__spawn_required_process_with_stdio(processes, &hundeschau_login_path, input,
                                                         IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT, true);
}

static bool HundeschauRequiredProcess__matches_pid(HundeschauRequiredProcessPtr self, void * context)
{
    return self->pid == *(uint64_t *)context;
}

static Optional(HundeschauRequiredProcessPtr)
    Hundeschau__find_required_process(HundeschauRequiredProcessesPtr processes, uint64_t pid)
{
    match_find_result_ptr(
        HundeschauRequiredProcesses__find(processes, HundeschauRequiredProcess__matches_pid, &pid),
        some(process)
        {
            return Some(HundeschauRequiredProcessPtr, process);
        },
        none_do_nothing()
    );
    return None(HundeschauRequiredProcessPtr);
}

static Optional(BString) Hundeschau__next_token(BStringPtr remaining)
{
    BStringSplit split = BString__split_first_token(remaining, &hundeschau_space_delimiters);
    *remaining = split.rest;
    return split.match;
}

static BString Hundeschau__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &hundeschau_comment_delimiter);
    match_optional(split.match,
        some(before_comment)
        {
            return before_comment;
        },
        none_do_nothing()
    );

    return BString__slice(line, 0, BString__length(line));
}

static Optional(DynString) Hundeschau__load_config(void)
{
    match_result(
        IO__open(&bstr(HUNDESCHAU_CONFIG_PATH), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                IO__read_dyn_string(handle, HUNDESCHAU_CONFIG_MAX),
                success(config)
                {
                    IO__close(handle);
                    if (DynString__length(&config) <= 0)
                    {
                        DynString__dispose(&config);
                        return None(DynString);
                    }
                    return Some(DynString, config);
                },
                error(code, message)
                {
                    IO__close(handle);
                    return None(DynString);
                }
            );
            return None(DynString);
        },
        error(code, message)
        {
            return None(DynString);
        }
    );
    return None(DynString);
}

static int Hundeschau__spawn_configured_driver_line(HundeschauRequiredProcessesPtr processes, ConstBStringPtr line,
                                                    bool framebuffer_present)
{
    BString command_line = Hundeschau__line_without_comment(line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return 0;
    }

    BString command = empty_bstr();
    match_optional(Hundeschau__next_token(&remaining),
        some(token)
        {
            command = token;
        },
        none()
        {
            return 0;
        }
    );

    if (!BString__equals(&command, &hundeschau_driver_command))
    {
        return 1;
    }

    BString path = empty_bstr();
    match_optional(Hundeschau__next_token(&remaining),
        some(token)
        {
            path = token;
        },
        none()
        {
            return 1;
        }
    );

    if (BString__is_empty(&path))
    {
        return 1;
    }

    bool respawn = true;
    BString option = empty_bstr();
    match_optional(Hundeschau__next_token(&remaining),
        some(token)
        {
            option = token;
        },
        none_do_nothing()
    );

    if (!BString__is_empty(&option))
    {
        if (BString__equals(&option, &hundeschau_once_option))
        {
            respawn = false;
            option = empty_bstr();
            match_optional(Hundeschau__next_token(&remaining),
                some(token)
                {
                    option = token;
                },
                none_do_nothing()
            );
        }
        else if (BString__equals(&option, &hundeschau_respawn_option))
        {
            respawn = true;
            option = empty_bstr();
            match_optional(Hundeschau__next_token(&remaining),
                some(token)
                {
                    option = token;
                },
                none_do_nothing()
            );
        }
    }

    bool enabled = true;
    if (!BString__is_empty(&option))
    {
        if (BString__equals(&option, &hundeschau_framebuffer_condition))
        {
            enabled = framebuffer_present;
        }
        else if (BString__equals(&option, &hundeschau_no_framebuffer_condition))
        {
            enabled = !framebuffer_present;
        }
        else if (!BString__equals(&option, &hundeschau_always_condition))
        {
            return 1;
        }

        option = empty_bstr();
        match_optional(Hundeschau__next_token(&remaining),
            some(token)
            {
                option = token;
            },
            none_do_nothing()
        );
    }

    if (!BString__is_empty(&option))
    {
        return 1;
    }

    if (!enabled)
    {
        return 0;
    }

    return Hundeschau__spawn_required_process(processes, &path, &hundeschau_empty, &hundeschau_empty, respawn);
}

static int Hundeschau__spawn_configured_drivers(HundeschauRequiredProcessesPtr processes, ConstDynStringPtr config,
                                                bool framebuffer_present)
{
    int failures = 0;

    match_optional(
        DynString__lines(config),
        some(lines)
        {
            for_each_ll(
                DynStringLines,
                lines,
                line,
                {
                    match_optional(
                        DynString__bstring(&line),
                        some(bstring_line)
                        {
                            failures += Hundeschau__spawn_configured_driver_line(processes, &bstring_line,
                                                                                 framebuffer_present);
                        },
                        none()
                        {
                            failures++;
                        }
                    );
                }
            );
            DynStringLines__dispose_all(&lines);
        },
        none()
        {
            failures++;
        }
    );

    return failures;
}

static int Hundeschau__spawn_default_drivers(HundeschauRequiredProcessesPtr processes, bool framebuffer_present)
{
    int failures = 0;
    failures += Hundeschau__spawn_required_process(processes, &hundeschau_humpen_path, &hundeschau_empty,
                                                   &hundeschau_empty, false);
    if (framebuffer_present)
    {
        failures += Hundeschau__spawn_required_process(processes, &hundeschau_video_term_path, &hundeschau_empty,
                                                       &hundeschau_empty, false);
    }

    return failures;
}

static int Hundeschau__initialise_required_processes(HundeschauRequiredProcessesPtr processes)
{
    int failures = 0;
    FramebufferConfig framebuffer_config = FramebufferConfig__new();
    bool framebuffer_present = FramebufferConfig__load(&framebuffer_config);

    match_optional(
        Hundeschau__load_config(),
        some(config)
        {
            failures += Hundeschau__spawn_configured_drivers(processes, &config, framebuffer_present);
            DynString__dispose(&config);
        },
        none()
        {
            failures += Hundeschau__spawn_default_drivers(processes, framebuffer_present);
        }
    );

    if (framebuffer_present)
    {
        if (!service_ok(service_wait_ready(&bstr("video_term"))) || !service_ok(service_wait_ready(&bstr("keyboard"))))
        {
            return 1;
        }
        failures += Hundeschau__spawn_login(processes, &hundeschau_keyboard_input);
    }
    else
    {
        failures += Hundeschau__spawn_login(processes, &hundeschau_terminal_input);
    }

    return failures;
}

int HUNDESCHAU_ENTRYPOINT(void)
{
    HundeschauRequiredProcesses required_processes = HundeschauRequiredProcesses__new();

    IO__printf(&bstr("🐶 HundeOS: hundeschau begins 🐾\n"));

    if (Hundeschau__initialise_required_processes(&required_processes) != 0)
    {
        return 1;
    }

    while (true)
    {
        ProcessChildEvent event = ProcessChildEvent__new();
        ProcessChildEventResult result = wait_for_child_event(&event);
        if (result < 0)
        {
            IO__printf(&bstr("hundeschau: wait child event failed: %i\n"), (int)result);
            return 1;
        }

        match_optional(
            Hundeschau__find_required_process(&required_processes, event.pid),
            some(process)
            {
                if (!process->respawn)
                {
                    continue;
                }

                Optional(BString) process_arg0 = SpawnArg__from_bstring(&process->arg0);
                Optional(BString) process_arg1 = SpawnArg__from_bstring(&process->arg1);
                SpawnResult respawn_result =
                    process->has_stdio
                        ? spawn_with_stdio(P_NOWAIT, &process->path, process->stdin_fd, process->stdout_fd,
                                           process->stderr_fd, &SPAWN_NO_ARG, &SPAWN_NO_ARG, &SPAWN_NO_ARG)
                        : spawn(P_NOWAIT, &process->path, &process_arg0, &process_arg1, &SPAWN_NO_ARG);
                if (respawn_result < 0)
                {
                    IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), &process->path, (int)respawn_result);
                    return 1;
                }

                process->pid = (uint64_t)respawn_result;
            },
            none_do_nothing()
        );
    }
}

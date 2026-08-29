#include "../../sys/spawn.h"
#include "../../sys/framebuffer_config.h"
#include "../../sys/io.h"
#include "../../sys/process.h"
#include "../../types/fixed_array.h"
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

OptionalDef(HundeschauRequiredProcess);
OptionalDef(HundeschauRequiredProcessPtr);

static bool HundeschauRequiredProcess__equals(HundeschauRequiredProcessPtr self, HundeschauRequiredProcessPtr other)
{
    return self->pid == other->pid;
}

static bool HundeschauRequiredProcess__equals_fixed_array(void * self, void * other)
{
    return HundeschauRequiredProcess__equals(
        (HundeschauRequiredProcessPtr) self,
        (HundeschauRequiredProcessPtr) other
    );
}

FixedArrayDefine(
    HundeschauRequiredProcess,
    HUNDESCHAU_REQUIRED_PROCESSES_MAX,
    HundeschauRequiredProcesses,
    &HundeschauRequiredProcess__equals_fixed_array
);
FixedArrayBodyDefine(HundeschauRequiredProcesses);

typedef HundeschauRequiredProcesses * HundeschauRequiredProcessesPtr;

static int Hundeschau__spawn_required_process(
    HundeschauRequiredProcessesPtr processes, ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, bool respawn
)
{
    SpawnResult result = spawn(
        P_NOWAIT,
        path,
        SpawnArg__from_bstring(arg0),
        SpawnArg__from_bstring(arg1),
        SPAWN_NO_ARG
    );
    if (result < 0)
    {
        IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), path, (int) result);
        return 1;
    }

    HundeschauRequiredProcess process = {
        .pid = (uint64_t) result,
        .respawn = respawn,
        .has_stdio = false,
        .path = BString__slice(path, 0, BString__length(path)),
        .arg0 = BString__slice(arg0, 0, BString__length(arg0)),
        .arg1 = BString__slice(arg1, 0, BString__length(arg1)),
    };
    match_result(CollectionCode, HundeschauRequiredProcesses__push(processes, process),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("hundeschau: failed to track process: %B\n"), &message);
            return 1;
        }
    );

    return 0;
}

static int Hundeschau__spawn_required_process_with_stdio(
    HundeschauRequiredProcessesPtr processes,
    ConstBStringPtr path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    bool respawn
)
{
    SpawnResult result = spawn_with_stdio(
        P_NOWAIT,
        path,
        stdin_fd,
        stdout_fd,
        stderr_fd,
        SPAWN_NO_ARG,
        SPAWN_NO_ARG,
        SPAWN_NO_ARG
    );
    if (result < 0)
    {
        IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), path, (int) result);
        return 1;
    }

    HundeschauRequiredProcess process = {
        .pid = (uint64_t) result,
        .respawn = respawn,
        .has_stdio = true,
        .stdin_fd = stdin_fd,
        .stdout_fd = stdout_fd,
        .stderr_fd = stderr_fd,
        .path = BString__slice(path, 0, BString__length(path)),
        .arg0 = empty_bstr(),
        .arg1 = empty_bstr(),
    };
    match_result(CollectionCode, HundeschauRequiredProcesses__push(processes, process),
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
    IOHandle stdin_fd = open(input_path, IO_OPEN_READ);
    if (stdin_fd < 0)
    {
        IO__printf(&bstr("hundeschau: failed to open terminal input: %i\n"), (int) stdin_fd);
        return 1;
    }

    IOHandle stdout_fd = open(&hundeschau_terminal_output, IO_OPEN_WRITE);
    if (stdout_fd < 0)
    {
        IO__printf(&bstr("hundeschau: failed to open terminal output: %i\n"), (int) stdout_fd);
        return 1;
    }

    IOHandle stderr_fd = open(&hundeschau_terminal_output, IO_OPEN_WRITE);
    if (stderr_fd < 0)
    {
        IO__printf(&bstr("hundeschau: failed to open terminal error output: %i\n"), (int) stderr_fd);
        return 1;
    }

    return Hundeschau__spawn_required_process_with_stdio(
        processes,
        &hundeschau_login_path,
        (SpawnFileDescriptor) stdin_fd,
        (SpawnFileDescriptor) stdout_fd,
        (SpawnFileDescriptor) stderr_fd,
        true
    );
}

static Optional(HundeschauRequiredProcessPtr) Hundeschau__find_required_process(
    HundeschauRequiredProcessesPtr processes, uint64_t pid
)
{
    for (uint64_t i = 0; i < HundeschauRequiredProcesses__len(processes); i++)
    {
        match_optional(HundeschauRequiredProcesses__type_ptr, HundeschauRequiredProcesses__ptr_at(processes, i),
            some(process)
            {
                if (process->pid == pid)
                {
                    return Some(HundeschauRequiredProcessPtr, process);
                }
            },
            none_do_nothing()
        );
    }

    return None(HundeschauRequiredProcessPtr);
}

static Optional(BString) Hundeschau__next_token(BStringPtr remaining)
{
    BString stripped = BString__strip(remaining);
    if (BString__is_empty(&stripped))
    {
        *remaining = empty_bstr();
        return None(BString);
    }

    BStringSplit split = BString__split_first_any(&stripped, &hundeschau_space_delimiters);
    match_optional(BString, split.match,
        some(token)
        {
            *remaining = split.rest;
            return Some(BString, token);
        },
        none()
        {
            *remaining = empty_bstr();
            return Some(BString, stripped);
        }
    );

    *remaining = empty_bstr();
    return None(BString);
}

static BString Hundeschau__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &hundeschau_comment_delimiter);
    match_optional(BString, split.match,
        some(before_comment) { return before_comment; },
        none_do_nothing()
    );

    return BString__slice(line, 0, BString__length(line));
}

static Optional(DynString) Hundeschau__load_config(void)
{
    BString config_path = bstr(HUNDESCHAU_CONFIG_PATH);
    IOHandle handle = open(&config_path, IO_OPEN_READ);
    if (handle < 0)
    {
        return None(DynString);
    }

    Optional(DynString) read_result = IO__read_dyn_string(handle, HUNDESCHAU_CONFIG_MAX);
    close(handle);

    match_optional(DynString, read_result,
        some(config)
        {
            if (DynString__length(&config) <= 0)
            {
                DynString__dispose(&config);
                return None(DynString);
            }

            return Some(DynString, config);
        },
        none_do_nothing()
    );

    return None(DynString);
}

static int Hundeschau__spawn_configured_driver_line(
    HundeschauRequiredProcessesPtr processes, BString line, bool framebuffer_present
)
{
    BString command_line = Hundeschau__line_without_comment(&line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return 0;
    }

    BString command = empty_bstr();
    match_optional(BString, Hundeschau__next_token(&remaining),
        some(token) { command = token; },
        none() { return 0; }
    );

    if (!BString__equals(&command, &hundeschau_driver_command))
    {
        return 1;
    }

    BString path = empty_bstr();
    match_optional(BString, Hundeschau__next_token(&remaining),
        some(token) { path = token; },
        none() { return 1; }
    );

    if (BString__is_empty(&path))
    {
        return 1;
    }

    bool respawn = true;
    BString option = empty_bstr();
    match_optional(BString, Hundeschau__next_token(&remaining),
        some(token) { option = token; },
        none_do_nothing()
    );

    if (!BString__is_empty(&option))
    {
        if (BString__equals(&option, &hundeschau_once_option))
        {
            respawn = false;
            option = empty_bstr();
            match_optional(BString, Hundeschau__next_token(&remaining),
                some(token) { option = token; },
                none_do_nothing()
            );
        }
        else if (BString__equals(&option, &hundeschau_respawn_option))
        {
            respawn = true;
            option = empty_bstr();
            match_optional(BString, Hundeschau__next_token(&remaining),
                some(token) { option = token; },
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
        match_optional(BString, Hundeschau__next_token(&remaining),
            some(token) { option = token; },
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

static int Hundeschau__spawn_configured_drivers(
    HundeschauRequiredProcessesPtr processes, ConstDynStringPtr config, bool framebuffer_present
)
{
    int failures = 0;

    match_optional(DynStringLines, DynString__lines(config),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line)
                    {
                        failures += Hundeschau__spawn_configured_driver_line(
                        processes,
                        bstring_line,
                        framebuffer_present
                    ); },
                    none() { failures++; }
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none() { failures++; }
    );

    return failures;
}

static int Hundeschau__spawn_default_drivers(
    HundeschauRequiredProcessesPtr processes, bool framebuffer_present
)
{
    int failures = 0;
    failures += Hundeschau__spawn_required_process(
        processes,
        &hundeschau_humpen_path,
        &hundeschau_empty,
        &hundeschau_empty,
        false
    );
    if (framebuffer_present)
    {
        failures += Hundeschau__spawn_required_process(
            processes,
            &hundeschau_video_term_path,
            &hundeschau_empty,
            &hundeschau_empty,
            false
        );
    }

    return failures;
}

static int Hundeschau__initialise_required_processes(HundeschauRequiredProcessesPtr processes)
{
    int failures = 0;
    FramebufferConfig framebuffer_config = { 0 };
    bool framebuffer_present = FramebufferConfig__load(&framebuffer_config);

    match_optional(DynString, Hundeschau__load_config(),
        some(config)
        {
            failures += Hundeschau__spawn_configured_drivers(processes, &config, framebuffer_present);
            DynString__dispose(&config);
        },
        none() { failures += Hundeschau__spawn_default_drivers(processes, framebuffer_present); }
    );

    if (framebuffer_present)
    {
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
            IO__printf(&bstr("hundeschau: wait child event failed: %i\n"), (int) result);
            return 1;
        }

        match_optional(HundeschauRequiredProcessPtr, Hundeschau__find_required_process(&required_processes, event.pid),
            some(process)
            {
                if (!process->respawn)
                {
                    continue;
                }

                SpawnResult respawn_result = process->has_stdio
                    ? spawn_with_stdio(
                        P_NOWAIT,
                        &process->path,
                        process->stdin_fd,
                        process->stdout_fd,
                        process->stderr_fd,
                        SPAWN_NO_ARG,
                        SPAWN_NO_ARG,
                        SPAWN_NO_ARG
                    )
                    : spawn(
                        P_NOWAIT,
                        &process->path,
                        SpawnArg__from_bstring(&process->arg0),
                        SpawnArg__from_bstring(&process->arg1),
                        SPAWN_NO_ARG
                    );
                if (respawn_result < 0)
                {
                    IO__printf(&bstr("hundeschau: failed to start %B: %i\n"), &process->path, (int) respawn_result);
                    return 1;
                }

                process->pid = (uint64_t) respawn_result;
            },
            none_do_nothing()
        );
    }
}

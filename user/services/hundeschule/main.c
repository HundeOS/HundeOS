#define DRIVER_LOG_TAG "HUNDESCHULE"

#include "sys/debug.h"
#include "mounts.h"
#include "sys/framebuffer_config.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/spawn.h"
#include "sys/system_info.h"
#include "trace/client.h"
#include "types/fixed_array.h"
#include "../../user.h"

#ifndef HUNDESCHULE_ENTRYPOINT
#define HUNDESCHULE_ENTRYPOINT main
#endif

#define HUNDESCHULE_REQUIRED_PROCESSES_MAX 16U
#define HUNDESCHULE_CONFIG_PATH "/boot/config/hundeschule.conf"
#define HUNDESCHULE_CONFIG_MAX 2048U

static BString hundeschule_empty = empty_bstr();
static BString hundeschule_hundeschau_path = bstr("/sbin/services/hundeschau");
static BString hundeschule_trace_path = bstr("/sbin/services/hund-verfolgen");
static BString hundeschule_hunde_bus_path = bstr("/sbin/drivers/hunde-bus");
static BString hundeschule_uart_rx_path = bstr("/sbin/drivers/uart_rx");
static BString hundeschule_video_term_path = bstr("/sbin/drivers/video_term");
static BString hundeschule_channel_manager_path = bstr("/sbin/services/hundekanal-warter");
static BString hundeschule_interrupt_manager_path = bstr("/sbin/services/hunde-treiber-host");
static BString hundeschule_io_host_path = bstr("/sbin/services/hunde-io-host");
static BString hundeschule_pipe_path = bstr("/sbin/services/hund-rohr");
static BString hundeschule_memfs_path = bstr("/sbin/drivers/memfs");
static BString hundeschule_procfs_path = bstr("/sbin/drivers/procfs");
static BString hundeschule_space_delimiters = bstr(" \t\r");
static BString hundeschule_comment_delimiter = bstr("#");
static BString hundeschule_driver_command = bstr("driver");
static BString hundeschule_once_option = bstr("once");
static BString hundeschule_respawn_option = bstr("respawn");
static BString hundeschule_framebuffer_condition = bstr("framebuffer");
static BString hundeschule_no_framebuffer_condition = bstr("no-framebuffer");
static BString hundeschule_always_condition = bstr("always");

typedef struct HundeschuleRequiredProcess
{
    uint64_t pid;
    bool respawn;
    BString path;
    BString arg0;
    BString arg1;
} HundeschuleRequiredProcess;

typedef HundeschuleRequiredProcess * HundeschuleRequiredProcessPtr;

OptionalDef(HundeschuleRequiredProcess);
OptionalDef(HundeschuleRequiredProcessPtr);

static HundeschuleRequiredProcess HundeschuleRequiredProcess__new(uint64_t pid, bool respawn, ConstBStringPtr path,
                                                                  ConstBStringPtr arg0, ConstBStringPtr arg1)
{
    return (HundeschuleRequiredProcess){
        .pid = pid,
        .respawn = respawn,
        .path = *path,
        .arg0 = *arg0,
        .arg1 = *arg1,
    };
}

static bool HundeschuleRequiredProcess__equals_fixed_array(void * self, void * other)
{
    return ((HundeschuleRequiredProcessPtr)self)->pid == ((HundeschuleRequiredProcessPtr)other)->pid;
}

FixedArrayDefine(HundeschuleRequiredProcess, HUNDESCHULE_REQUIRED_PROCESSES_MAX, HundeschuleRequiredProcesses,
                 &HundeschuleRequiredProcess__equals_fixed_array);
FixedArrayBodyDefine(HundeschuleRequiredProcesses);

typedef HundeschuleRequiredProcesses * HundeschuleRequiredProcessesPtr;

static int Hundeschule__spawn_required_process(HundeschuleRequiredProcessesPtr processes, ConstBStringPtr path,
                                               ConstBStringPtr arg0, ConstBStringPtr arg1, bool respawn)
{
    Optional(BString) process_arg0 = SpawnArg__from_bstring(arg0);
    Optional(BString) process_arg1 = SpawnArg__from_bstring(arg1);
    SpawnResult result = spawn(P_NOWAIT, path, &process_arg0, &process_arg1, &SPAWN_NO_ARG);
    if (result < 0)
    {
        DRIVER_DEBUG("hundeschule: failed to start %B: %i\n", path, (int)result);
        IO__printf(&bstr("hundeschule: failed to start %B: %i\n"), path, (int)result);
        return 1;
    }

    HundeschuleRequiredProcess process = HundeschuleRequiredProcess__new((uint64_t)result, respawn, path, arg0, arg1);
    match_result(
        HundeschuleRequiredProcesses__push(processes, process),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("hundeschule: failed to track process: %B\n"), &message);
            return 1;
        }
    );

    return 0;
}

static bool HundeschuleRequiredProcess__matches_pid(HundeschuleRequiredProcessPtr self, void * context)
{
    return self->pid == *(uint64_t *)context;
}

static Optional(HundeschuleRequiredProcessPtr)
    Hundeschule__find_required_process(HundeschuleRequiredProcessesPtr processes, uint64_t pid)
{
    match_find_result_ptr(
        HundeschuleRequiredProcesses__find(processes, HundeschuleRequiredProcess__matches_pid, &pid),
        some(process)
        {
            return Some(HundeschuleRequiredProcessPtr, process);
        },
        none_do_nothing()
    );
    return None(HundeschuleRequiredProcessPtr);
}

static Optional(DynString) Hundeschule__load_config(void);
static int Hundeschule__spawn_configured_drivers(HundeschuleRequiredProcessesPtr processes, ConstDynStringPtr config,
                                                 bool framebuffer_present);
static int Hundeschule__spawn_default_drivers(HundeschuleRequiredProcessesPtr processes, bool framebuffer_present);

static int Hundeschule__spawn_machine_drivers(HundeschuleRequiredProcessesPtr processes, bool framebuffer_present)
{
    Optional(DynString) config = Hundeschule__load_config();
    int failures = 0;
    match_optional(
        config,
        some(loaded_config)
        {
            failures = Hundeschule__spawn_configured_drivers(processes, &loaded_config, framebuffer_present);
            DynString__dispose(&loaded_config);
        },
        none()
        {
            failures = Hundeschule__spawn_default_drivers(processes, framebuffer_present);
        }
    );
    return failures;
}

static Optional(BString) Hundeschule__next_token(BStringPtr remaining)
{
    BStringSplit split = BString__split_first_token(remaining, &hundeschule_space_delimiters);
    *remaining = split.rest;
    return split.match;
}

static BString Hundeschule__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &hundeschule_comment_delimiter);
    match_optional(split.match,
        some(before_comment)
        {
            return before_comment;
        },
        none_do_nothing()
    );

    return BString__slice(line, 0, BString__length(line));
}

static Optional(DynString) Hundeschule__load_config(void)
{
    match_result(
        IO__open(&bstr(HUNDESCHULE_CONFIG_PATH), IO_OPEN_READ),
        success(handle)
        {
            match_result(
                IO__read_dyn_string(handle, HUNDESCHULE_CONFIG_MAX),
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

static int Hundeschule__spawn_configured_driver_line(HundeschuleRequiredProcessesPtr processes, ConstBStringPtr line,
                                                     bool framebuffer_present)
{
    BString command_line = Hundeschule__line_without_comment(line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return 0;
    }

    BString command = empty_bstr();
    match_optional(Hundeschule__next_token(&remaining),
        some(token)
        {
            command = token;
        },
        none()
        {
            return 0;
        }
    );

    if (!BString__equals(&command, &hundeschule_driver_command))
    {
        DRIVER_DEBUG("hundeschule: invalid config command=%B line=%B\n", &command, &line);
        return 1;
    }

    BString path = empty_bstr();
    match_optional(Hundeschule__next_token(&remaining),
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
    match_optional(Hundeschule__next_token(&remaining),
        some(token)
        {
            option = token;
        },
        none_do_nothing()
    );

    if (!BString__is_empty(&option))
    {
        if (BString__equals(&option, &hundeschule_once_option))
        {
            respawn = false;
            option = empty_bstr();
            match_optional(Hundeschule__next_token(&remaining),
                some(token)
                {
                    option = token;
                },
                none_do_nothing()
            );
        }
        else if (BString__equals(&option, &hundeschule_respawn_option))
        {
            respawn = true;
            option = empty_bstr();
            match_optional(Hundeschule__next_token(&remaining),
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
        if (BString__equals(&option, &hundeschule_framebuffer_condition))
        {
            enabled = framebuffer_present;
        }
        else if (BString__equals(&option, &hundeschule_no_framebuffer_condition))
        {
            enabled = !framebuffer_present;
        }
        else if (!BString__equals(&option, &hundeschule_always_condition))
        {
            DRIVER_DEBUG("hundeschule: invalid config condition=%B line=%B\n", &option, &line);
            return 1;
        }

        option = empty_bstr();
        match_optional(Hundeschule__next_token(&remaining),
            some(token)
            {
                option = token;
            },
            none_do_nothing()
        );
    }

    if (!BString__is_empty(&option))
    {
        DRIVER_DEBUG("hundeschule: unexpected config token=%B line=%B\n", &option, &line);
        return 1;
    }

    if (!enabled)
    {
        DRIVER_DEBUG("hundeschule: config skipped path=%B line=%B\n", &path, &line);
        return 0;
    }

    // These services are already running before disk configuration is available.
    if (BString__equals(&path, &hundeschule_channel_manager_path) ||
        BString__equals(&path, &hundeschule_interrupt_manager_path) ||
        BString__equals(&path, &hundeschule_uart_rx_path) || BString__equals(&path, &bstr("/sbin/drivers/uart_tx")) ||
        BString__equals(&path, &hundeschule_trace_path) || BString__equals(&path, &hundeschule_io_host_path) ||
        BString__equals(&path, &hundeschule_pipe_path) || BString__equals(&path, &hundeschule_memfs_path) ||
        BString__equals(&path, &hundeschule_procfs_path))
    {
        return 0;
    }

    DRIVER_DEBUG("hundeschule: config spawning path=%B respawn=%i\n", &path, (int)respawn);
    return Hundeschule__spawn_required_process(processes, &path, &hundeschule_empty, &hundeschule_empty, respawn);
}

static int Hundeschule__spawn_configured_drivers(HundeschuleRequiredProcessesPtr processes, ConstDynStringPtr config,
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
                            failures += Hundeschule__spawn_configured_driver_line(processes, &bstring_line,
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

static int Hundeschule__spawn_default_drivers(HundeschuleRequiredProcessesPtr processes, bool framebuffer_present)
{
    int failures = 0;
    failures += Hundeschule__spawn_required_process(processes, &hundeschule_hunde_bus_path, &hundeschule_empty,
                                                    &hundeschule_empty, false);
    if (framebuffer_present)
    {
        failures += Hundeschule__spawn_required_process(processes, &hundeschule_video_term_path, &hundeschule_empty,
                                                        &hundeschule_empty, false);
    }
    return failures;
}

static int Hundeschule__initialise_required_processes(HundeschuleRequiredProcessesPtr processes)
{
    int failures = 0;
    FramebufferConfig framebuffer_config = FramebufferConfig__new();
    bool framebuffer_present = FramebufferConfig__load(&framebuffer_config);

    if (!service_ok(service_become_service()))
    {
        IO__printf(&bstr("hundeschule: service registration failed\n"));
        return 1;
    }

    if (!service_ok(service_publish_ready(&bstr("hundeschule"))) ||
        !service_ok(service_wait_ready(&bstr("hunde-mama"))))
    {
        return 1;
    }

    bool io_latency_tracing_enabled = false;
    match_result(
        SystemSettings__load(),
        success(settings)
        {
            io_latency_tracing_enabled = settings.io_latency_tracing_enabled;
        },
        error(code, message)
        {
            DRIVER_DEBUG("hundeschule: system settings unavailable: %B (%i)\n", &message, (int)code);
        }
    );

    if (io_latency_tracing_enabled &&
        (Hundeschule__spawn_required_process(processes, &hundeschule_trace_path, &hundeschule_empty, &hundeschule_empty,
                                             true) != 0 ||
         !service_ok(service_wait_ready(&bstr(HUND_VERFOLGEN_SERVICE)))))
    {
        return 1;
    }

    if (!service_ok(service_wait_ready(&bstr("socket_router"))))
    {
        return 1;
    }

    failures += Hundeschule__spawn_machine_drivers(processes, framebuffer_present);

    if (!framebuffer_present)
    {
        ServiceCode uart_rx_status = service_wait_ready(&bstr("uart_rx"));
        if (!service_ok(uart_rx_status))
        {
            return 1;
        }
        ServiceCode uart_tx_status = service_wait_ready(&bstr("uart_tx"));
        if (!service_ok(uart_tx_status))
        {
            return 1;
        }
    }

    if (failures != 0)
    {
        return failures;
    }

    match_result(
        Hundeschule__mount_filesystems(),
        success_do_nothing(),
        error(code, message)
        {
            DRIVER_DEBUG("hundeschule: mounts failed: %B (%i)\n", &message, (int)code);
            return 1;
        }
    );

    ServiceCode pipe_status = service_wait_ready(&bstr(IO_PIPE_SERVICE));
    if (!service_ok(pipe_status))
    {
        return 1;
    }
    failures += Hundeschule__spawn_required_process(processes, &hundeschule_hundeschau_path, &hundeschule_empty,
                                                    &hundeschule_empty, false);

    return failures;
}

int HUNDESCHULE_ENTRYPOINT(void)
{
    HundeschuleRequiredProcesses required_processes = HundeschuleRequiredProcesses__new();

    IO__printf(&bstr("🐶 HundeOS: hundeschule begins 🎓\n"));

    if (Hundeschule__initialise_required_processes(&required_processes) != 0)
    {
        return 1;
    }

    while (true)
    {
        ProcessChildEvent event = ProcessChildEvent__new();
        ProcessChildEventResult result = wait_for_child_event(&event);
        if (result < 0)
        {
            IO__printf(&bstr("hundeschule: wait child event failed: %i\n"), (int)result);
            return 1;
        }

        match_optional(
            Hundeschule__find_required_process(&required_processes, event.pid),
            some(process)
            {
                if (!process->respawn)
                {
                    continue;
                }

                Optional(BString) process_arg0 = SpawnArg__from_bstring(&process->arg0);
                Optional(BString) process_arg1 = SpawnArg__from_bstring(&process->arg1);
                SpawnResult respawn_result =
                    spawn(P_NOWAIT, &process->path, &process_arg0, &process_arg1, &SPAWN_NO_ARG);
                if (respawn_result < 0)
                {
                    IO__printf(&bstr("hundeschule: failed to start %B: %i\n"), &process->path, (int)respawn_result);
                    return 1;
                }

                process->pid = (uint64_t)respawn_result;
            },
            none_do_nothing()
        );
    }
}

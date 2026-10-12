#define DRIVER_LOG_TAG "HUNDESCHULE"

#include "sys/debug.h"
#include "mounts.h"
#include "sys/boot_info.h"
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

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK 2U
#define HUNDESCHULE_REQUIRED_PROCESSES_MAX 16U
#define HUNDESCHULE_VIRTIO_BLOCK_DRIVER_SELECTIONS_MAX 16U
#define HUNDESCHULE_CONFIG_PATH "/boot/config/hundeschule.conf"
#define HUNDESCHULE_CONFIG_MAX 2048U

static BString hundeschule_empty = empty_bstr();
static BString hundeschule_hundeschau_path = bstr("/sbin/services/hundeschau");
static BString hundeschule_trace_path = bstr("/sbin/services/hund-verfolgen");
static BString hundeschule_hundebus_path = bstr("/sbin/drivers/hundebus");
static BString hundeschule_virtio_keyboard_path = bstr("/sbin/drivers/virtio_keyboard");
static BString hundeschule_uart_rx_path = bstr("/sbin/drivers/uart_rx");
static BString hundeschule_uart_tx_path = bstr("/sbin/drivers/uart_tx");
static BString hundeschule_channel_manager_path = bstr("/sbin/services/hundekanal-warter");
static BString hundeschule_interrupt_manager_path = bstr("/sbin/services/hund-unterbrict");
static BString hundeschule_virtio_blk_channelio_path = bstr("/sbin/drivers/virtio_blk_channelio");
static BString hundeschule_socket_router_path = bstr("/sbin/drivers/socket_router");
static BString hundeschule_filesystem_path = bstr("/sbin/services/hund-dateisystem");
static BString hundeschule_io_system_path = bstr("/sbin/services/hund-io-system");
static BString hundeschule_pipe_path = bstr("/sbin/services/hund-rohr");
static BString hundeschule_space_delimiters = bstr(" \t\r");
static BString hundeschule_comment_delimiter = bstr("#");
static BString hundeschule_driver_command = bstr("driver");
static BString hundeschule_virtio_block_driver_command = bstr("virtio-block-driver");
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
typedef volatile uint32_t * HundeschuleRegisterPtr;

typedef struct HundeschuleVirtioBlockDriverSelection
{
    BString device_path;
    BString driver_path;
    bool used;
} HundeschuleVirtioBlockDriverSelection;

typedef HundeschuleVirtioBlockDriverSelection * HundeschuleVirtioBlockDriverSelectionPtr;

OptionalDef(HundeschuleVirtioBlockDriverSelection);

static HundeschuleVirtioBlockDriverSelection HundeschuleVirtioBlockDriverSelection__new(ConstBStringPtr device_path,
                                                                                        ConstBStringPtr driver_path)
{
    return (HundeschuleVirtioBlockDriverSelection){
        .device_path = *device_path,
        .driver_path = *driver_path,
        .used = false,
    };
}

static bool HundeschuleVirtioBlockDriverSelection__equals_fixed_array(void * self, void * other)
{
    HundeschuleVirtioBlockDriverSelectionPtr left = self;
    HundeschuleVirtioBlockDriverSelectionPtr right = other;
    return BString__equals(&left->device_path, &right->device_path);
}

FixedArrayDefine(HundeschuleVirtioBlockDriverSelection, HUNDESCHULE_VIRTIO_BLOCK_DRIVER_SELECTIONS_MAX,
                 HundeschuleVirtioBlockDriverSelections, &HundeschuleVirtioBlockDriverSelection__equals_fixed_array);
FixedArrayBodyDefine(HundeschuleVirtioBlockDriverSelections);

typedef HundeschuleVirtioBlockDriverSelections * HundeschuleVirtioBlockDriverSelectionsPtr;

static HundeschuleRegisterPtr Hundeschule__reg32(void * base, uint64_t offset)
{
    return (HundeschuleRegisterPtr)((uint64_t)base + offset);
}

static uint32_t Hundeschule__read32(void * base, uint64_t offset)
{
    return *Hundeschule__reg32(base, offset);
}

static BString Hundeschule__u64_bstring(uint64_t value)
{
    return BString__sprintf(&bstr("%llu"), value);
}

static BString Hundeschule__virtio_path(uint64_t ordinal)
{
    return BString__sprintf(&bstr("/dev/virtio%llu"), ordinal);
}

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

static bool HundeschuleVirtioBlockDriverSelection__matches_device(HundeschuleVirtioBlockDriverSelectionPtr self,
                                                                  void * context)
{
    return BString__equals(&self->device_path, context);
}

static ConstBStringPtr Hundeschule__virtio_block_driver(HundeschuleVirtioBlockDriverSelectionsPtr selections,
                                                        ConstBStringPtr device_path)
{
    match_find_result_ptr(
        HundeschuleVirtioBlockDriverSelections__find(selections, HundeschuleVirtioBlockDriverSelection__matches_device,
                                                     (void *)device_path),
        some(selection)
        {
            selection->used = true;
            return &selection->driver_path;
        },
        none_do_nothing()
    );
    return &hundeschule_virtio_blk_channelio_path;
}

static bool Hundeschule__is_virtio_block_driver(ConstBStringPtr path)
{
    return BString__equals(path, &hundeschule_virtio_blk_channelio_path);
}

static int Hundeschule__wait_for_block_device(ConstBStringPtr device_path)
{
    ServiceCode result = service_wait_ready(device_path);
    if (service_ok(result))
    {
        return 0;
    }

    IO__printf(&bstr("hundeschule: failed waiting for %B: %i\n"), device_path, (int)result);
    return 1;
}

static int Hundeschule__unused_virtio_block_driver_selections(HundeschuleVirtioBlockDriverSelectionsPtr selections)
{
    int failures = 0;
    for_each_fa_ptr(
        HundeschuleVirtioBlockDriverSelections,
        selections,
        index,
        selection,
        {
            if (!selection.used)
            {
                IO__printf(&bstr("hundeschule: virtio block driver selection did not match %B\n"),
                           &selection.device_path);
                failures++;
            }
        }
    );
    return failures;
}

static Optional(DynString) Hundeschule__load_config(void);
static int Hundeschule__parse_virtio_block_driver_selections(HundeschuleVirtioBlockDriverSelectionsPtr selections,
                                                             ConstDynStringPtr config);
static int Hundeschule__spawn_configured_drivers(HundeschuleRequiredProcessesPtr processes, ConstDynStringPtr config,
                                                 bool framebuffer_present);
static int Hundeschule__spawn_default_drivers(HundeschuleRequiredProcessesPtr processes, bool framebuffer_present);

static int Hundeschule__spawn_virtio_block_drivers(HundeschuleRequiredProcessesPtr processes, bool framebuffer_present)
{
    BootHardwareInfo hardware;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            hardware = info;
        },
        error(code, message)
        {
            IO__printf(&bstr("hundeschule: boot hardware unavailable\n"));
            return 1;
        }
    );

    HundeschuleVirtioBlockDriverSelections selections = HundeschuleVirtioBlockDriverSelections__new();
    Optional(DynString) config = Hundeschule__load_config();
    bool configured_services_started = is_something(config);
    int failures = 0;
    match_optional(
        config,
        some(loaded_config)
        {
            failures += Hundeschule__parse_virtio_block_driver_selections(&selections, &loaded_config);
        },
        none_do_nothing()
    );

    match_optional(
        hardware.virtio_mmio,
        some(virtio_mmio)
        {
            ServiceMappedMemoryPtr mapping;
            if (!service_ok(service_map_mmio(virtio_mmio.physical_base,
                                             virtio_mmio.slot_size * virtio_mmio.slot_count, &mapping)))
            {
                failures++;
            }
            else
            {
                uint64_t virtio_ordinal = 1;
                for (uint64_t next_slot = virtio_mmio.slot_count; next_slot > 0; next_slot--)
                {
                    uint64_t slot = next_slot - 1;
                    void * mmio = driver_ptr_offset(mapping, slot * virtio_mmio.slot_size);
                    uint32_t magic = Hundeschule__read32(mmio, VIRTIO_MMIO_MAGIC);
                    uint32_t version = Hundeschule__read32(mmio, VIRTIO_MMIO_VERSION);
                    uint32_t device_id = Hundeschule__read32(mmio, VIRTIO_MMIO_DEVICE_ID);
                    if (magic != VIRTIO_MMIO_MAGIC_VALUE || version < 1 || device_id != VIRTIO_DEVICE_ID_BLOCK)
                    {
                        continue;
                    }

                    BString slot_arg = Hundeschule__u64_bstring(slot);
                    BString path_arg = Hundeschule__virtio_path(virtio_ordinal);
                    ConstBStringPtr driver_path = Hundeschule__virtio_block_driver(&selections, &path_arg);
                    bool already_started =
                        is_something(ServiceDirectory__find_service_pid(ServiceDirectory__shared(), &path_arg));
                    if (!already_started)
                    {
                        failures +=
                            Hundeschule__spawn_required_process(processes, driver_path, &slot_arg, &path_arg, true);
                    }
                    if (failures == 0)
                    {
                        failures += Hundeschule__wait_for_block_device(&path_arg);
                    }
                    virtio_ordinal++;
                    if (failures != 0)
                    {
                        break;
                    }
                }
            }
        },
        none_do_nothing()
    );

    if (failures == 0 && configured_services_started)
    {
        match_optional(
            config,
            some(loaded_config)
            {
                failures += Hundeschule__spawn_configured_drivers(processes, &loaded_config, framebuffer_present);
            },
            none_do_nothing()
        );
    }

    if (failures == 0 && !configured_services_started)
    {
        failures += Hundeschule__spawn_default_drivers(processes, framebuffer_present);
    }

    failures += Hundeschule__unused_virtio_block_driver_selections(&selections);
    match_optional(config,
        some(loaded_config)
        {
            DynString__dispose(&loaded_config);
        },
        none_do_nothing()
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
    IOHandle handle = IO__open(&bstr(HUNDESCHULE_CONFIG_PATH), IO_OPEN_READ);
    if (handle < 0)
    {
        return None(DynString);
    }

    Optional(DynString) read_result = IO__read_dyn_string(handle, HUNDESCHULE_CONFIG_MAX);
    IO__close(handle);

    match_optional(
        read_result,
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

static int Hundeschule__parse_virtio_block_driver_selection_line(HundeschuleVirtioBlockDriverSelectionsPtr selections,
                                                                 ConstBStringPtr line)
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

    if (!BString__equals(&command, &hundeschule_virtio_block_driver_command))
    {
        return 0;
    }

    BString device_path = empty_bstr();
    match_optional(Hundeschule__next_token(&remaining),
        some(token)
        {
            device_path = token;
        },
        none()
        {
            return 1;
        }
    );

    BString driver_path = empty_bstr();
    match_optional(Hundeschule__next_token(&remaining),
        some(token)
        {
            driver_path = token;
        },
        none()
        {
            return 1;
        }
    );

    match_optional(Hundeschule__next_token(&remaining),
        some(unexpected)
        {
            return 1;
        },
        none_do_nothing()
    );

    if (!Hundeschule__is_virtio_block_driver(&driver_path))
    {
        DRIVER_DEBUG("hundeschule: invalid virtio block driver=%B line=%B\n", &driver_path, line);
        return 1;
    }

    HundeschuleVirtioBlockDriverSelection selection =
        HundeschuleVirtioBlockDriverSelection__new(&device_path, &driver_path);
    match_optional(
        HundeschuleVirtioBlockDriverSelections__find_item(selections, selection),
        some(index)
        {
            return 1;
        },
        none_do_nothing()
    );
    match_result(
        HundeschuleVirtioBlockDriverSelections__push(selections, selection),
        success_do_nothing(),
        error(code, message)
        {
            return 1;
        }
    );
    return 0;
}

static int Hundeschule__parse_virtio_block_driver_selections(HundeschuleVirtioBlockDriverSelectionsPtr selections,
                                                             ConstDynStringPtr config)
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
                            failures += Hundeschule__parse_virtio_block_driver_selection_line(selections,
                                                                                              &bstring_line);
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

    if (BString__equals(&command, &hundeschule_virtio_block_driver_command))
    {
        return 0;
    }

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
        BString__equals(&path, &hundeschule_trace_path) || BString__equals(&path, &hundeschule_socket_router_path) ||
        BString__equals(&path, &hundeschule_io_system_path) || BString__equals(&path, &hundeschule_filesystem_path))
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
    failures += Hundeschule__spawn_required_process(processes, &hundeschule_hundebus_path, &hundeschule_empty,
                                                    &hundeschule_empty, false);
    if (framebuffer_present)
    {
        failures += Hundeschule__spawn_required_process(processes, &hundeschule_virtio_keyboard_path,
                                                        &hundeschule_empty, &hundeschule_empty, true);
    }
    else
    {
        failures += Hundeschule__spawn_required_process(processes, &hundeschule_uart_rx_path, &hundeschule_empty,
                                                        &hundeschule_empty, true);
        failures += Hundeschule__spawn_required_process(processes, &hundeschule_uart_tx_path, &hundeschule_empty,
                                                        &hundeschule_empty, true);
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
        (Hundeschule__spawn_required_process(processes, &hundeschule_trace_path, &hundeschule_empty,
                                             &hundeschule_empty, true) != 0 ||
         !service_ok(service_wait_ready(&bstr(HUND_VERFOLGEN_SERVICE)))))
    {
        return 1;
    }

    if (Hundeschule__spawn_required_process(processes, &hundeschule_socket_router_path, &hundeschule_empty,
                                            &hundeschule_empty, false) != 0 ||
        !service_ok(service_wait_ready(&bstr("socket_router"))))
    {
        return 1;
    }

    failures += Hundeschule__spawn_virtio_block_drivers(processes, framebuffer_present);

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

    if (Hundeschule__spawn_required_process(processes, &hundeschule_pipe_path, &hundeschule_empty, &hundeschule_empty,
                                            false) != 0)
    {
        return 1;
    }
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

    IO__printf(&bstr("HundeOS: hundeschule begins\n"));

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

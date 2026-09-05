#define DRIVER_LOG_TAG "HUNDESCHULE"

#include "sys/debug.h"
#include "sys/framebuffer_config.h"
#include "sys/io.h"
#include "sys/process.h"
#include "sys/service.h"
#include "sys/spawn.h"
#include "types/fixed_array.h"
#include "../../user.h"

#ifndef HUNDESCHULE_ENTRYPOINT
#define HUNDESCHULE_ENTRYPOINT main
#endif

#define VIRTIO_MMIO_FIRST_PHYS_BASE 0x0a000000ULL
#define VIRTIO_MMIO_SLOT_SIZE       0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT      32ULL
#define VIRTIO_MMIO_REGION_SIZE     (VIRTIO_MMIO_SLOT_SIZE * VIRTIO_MMIO_SLOT_COUNT)

#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK  2U
#define HUNDESCHULE_REQUIRED_PROCESSES_MAX 16U
#define HUNDESCHULE_CONFIG_PATH "/boot/config/hundeschule.conf"
#define HUNDESCHULE_CONFIG_MAX 2048U

static BString hundeschule_empty = empty_bstr();
static BString hundeschule_hundeschau_path = bstr("/sbin/services/hundeschau");
static BString hundeschule_hundebus_path = bstr("/sbin/drivers/hundebus");
static BString hundeschule_virtio_keyboard_path = bstr("/sbin/drivers/virtio_keyboard");
static BString hundeschule_uart_rx_path = bstr("/sbin/drivers/uart_rx");
static BString hundeschule_uart_tx_path = bstr("/sbin/drivers/uart_tx");
static BString hundeschule_virtio_blk_path = bstr("/sbin/drivers/virtio_blk");
static BString hundeschule_socket_router_path = bstr("/sbin/drivers/socket_router");
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

static HundeschuleRequiredProcess HundeschuleRequiredProcess__new(
    uint64_t pid,
    bool respawn,
    ConstBStringPtr path,
    ConstBStringPtr arg0,
    ConstBStringPtr arg1)
{
    return (HundeschuleRequiredProcess) {
        .pid = pid,
        .respawn = respawn,
        .path = *path,
        .arg0 = *arg0,
        .arg1 = *arg1,
    };
}

static bool HundeschuleRequiredProcess__equals_fixed_array(void * self, void * other)
{
    return ((HundeschuleRequiredProcessPtr) self)->pid == ((HundeschuleRequiredProcessPtr) other)->pid;
}

FixedArrayDefine(
    HundeschuleRequiredProcess,
    HUNDESCHULE_REQUIRED_PROCESSES_MAX,
    HundeschuleRequiredProcesses,
    &HundeschuleRequiredProcess__equals_fixed_array
);
FixedArrayBodyDefine(HundeschuleRequiredProcesses);

typedef HundeschuleRequiredProcesses * HundeschuleRequiredProcessesPtr;
typedef volatile uint32_t * HundeschuleRegisterPtr;

static HundeschuleRegisterPtr Hundeschule__reg32(void * base, uint64_t offset)
{
    return (HundeschuleRegisterPtr) ((uint64_t) base + offset);
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

static int Hundeschule__spawn_required_process(
    HundeschuleRequiredProcessesPtr processes, ConstBStringPtr path, ConstBStringPtr arg0, ConstBStringPtr arg1, bool respawn
)
{
    Optional(BString) process_arg0 = SpawnArg__from_bstring(arg0);
    Optional(BString) process_arg1 = SpawnArg__from_bstring(arg1);
    SpawnResult result = spawn(
        P_NOWAIT,
        path,
        &process_arg0,
        &process_arg1,
        &SPAWN_NO_ARG
    );
    if (result < 0)
    {
        DRIVER_DEBUG("hundeschule: failed to start %B: %i\n", path, (int) result);
        IO__printf(&bstr("hundeschule: failed to start %B: %i\n"), path, (int) result);
        return 1;
    }

    DRIVER_DEBUG(
        "hundeschule: started path=%B pid=%i respawn=%i arg0=%B arg1=%B\n",
        path,
        (int) result,
        (int) respawn,
        arg0,
        arg1
    );

    HundeschuleRequiredProcess process = HundeschuleRequiredProcess__new(
        (uint64_t) result,
        respawn,
        path,
        arg0,
        arg1
    );
    match_result(CollectionCode, HundeschuleRequiredProcesses__push(processes, process),
        success_do_nothing(),
        error(code, message)
        {
            IO__printf(&bstr("hundeschule: failed to track process: %B\n"), &message);
            return 1;
        }
    );

    return 0;
}

static Optional(HundeschuleRequiredProcessPtr) Hundeschule__find_required_process(
    HundeschuleRequiredProcessesPtr processes, uint64_t pid
)
{
    for (uint64_t i = 0; i < HundeschuleRequiredProcesses__len(processes); i++)
    {
        match_optional(HundeschuleRequiredProcesses__type_ptr, HundeschuleRequiredProcesses__ptr_at(processes, i),
            some(process)
            {
                if (process->pid == pid)
                {
                    return Some(HundeschuleRequiredProcessPtr, process);
                }
            },
            none_do_nothing()
        );
    }

    return None(HundeschuleRequiredProcessPtr);
}

static Optional(BString) HundeschuleRequiredProcess__ready_path(HundeschuleRequiredProcessPtr self)
{
    if (BString__equals(&self->path, &hundeschule_virtio_blk_path))
    {
        return Some(BString, self->arg1);
    }

    return None(BString);
}

static int Hundeschule__wait_for_bootstrap_block_devices(HundeschuleRequiredProcessesPtr processes)
{
    int failures = 0;
    for (uint64_t i = 0; i < HundeschuleRequiredProcesses__len(processes); i++)
    {
        match_optional(HundeschuleRequiredProcesses__type_ptr, HundeschuleRequiredProcesses__ptr_at(processes, i),
            some(process)
            {
                match_optional(BString, HundeschuleRequiredProcess__ready_path(process),
                    some(ready_path)
                    {
                        ServiceCode result = service_wait_ready(&ready_path);
                        if (!service_ok(result))
                        {
                            IO__printf(&bstr("hundeschule: failed waiting for %B: %i\n"), &ready_path, (int) result);
                            failures++;
                        }
                    },
                    none_do_nothing()
                );
            },
            none_do_nothing()
        );
    }

    return failures;
}

static int Hundeschule__spawn_virtio_block_drivers(HundeschuleRequiredProcessesPtr processes)
{
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(VIRTIO_MMIO_FIRST_PHYS_BASE, VIRTIO_MMIO_REGION_SIZE, &mapping)))
    {
        IO__printf(&bstr("hundeschule: failed to probe virtio mmio\n"));
        return 1;
    }

    int failures = 0;
    uint64_t virtio_ordinal = 1;
    for (uint64_t next_slot = VIRTIO_MMIO_SLOT_COUNT; next_slot > 0; next_slot--)
    {
        uint64_t slot = next_slot - 1;
        void * mmio = driver_ptr_offset(mapping, slot * VIRTIO_MMIO_SLOT_SIZE);
        uint32_t magic = Hundeschule__read32(mmio, VIRTIO_MMIO_MAGIC);
        uint32_t version = Hundeschule__read32(mmio, VIRTIO_MMIO_VERSION);
        uint32_t device_id = Hundeschule__read32(mmio, VIRTIO_MMIO_DEVICE_ID);

        if (magic == VIRTIO_MMIO_MAGIC_VALUE && version >= 1 && device_id == VIRTIO_DEVICE_ID_BLOCK)
        {
            BString slot_arg = Hundeschule__u64_bstring(slot);
            BString path_arg = Hundeschule__virtio_path(virtio_ordinal);
            failures += Hundeschule__spawn_required_process(
                processes,
                &hundeschule_virtio_blk_path,
                &slot_arg,
                &path_arg,
                true
            );
            virtio_ordinal++;
        }
    }

    return failures;
}

static Optional(BString) Hundeschule__next_token(BStringPtr remaining)
{
    BString stripped = BString__strip(remaining);
    if (BString__is_empty(&stripped))
    {
        *remaining = empty_bstr();
        return None(BString);
    }

    BStringSplit split = BString__split_first_any(&stripped, &hundeschule_space_delimiters);
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

static BString Hundeschule__line_without_comment(ConstBStringPtr line)
{
    BStringSplit split = BString__split_first_any(line, &hundeschule_comment_delimiter);
    match_optional(BString, split.match,
        some(before_comment) { return before_comment; },
        none_do_nothing()
    );

    return BString__slice(line, 0, BString__length(line));
}

static Optional(DynString) Hundeschule__load_config(void)
{
    IOHandle handle = open(&bstr(HUNDESCHULE_CONFIG_PATH), IO_OPEN_READ);
    if (handle < 0)
    {
        return None(DynString);
    }

    Optional(DynString) read_result = IO__read_dyn_string(handle, HUNDESCHULE_CONFIG_MAX);
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

static int Hundeschule__spawn_configured_driver_line(
    HundeschuleRequiredProcessesPtr processes, ConstBStringPtr line, bool framebuffer_present
)
{
    BString command_line = Hundeschule__line_without_comment(line);
    BString remaining = BString__strip(&command_line);
    if (BString__is_empty(&remaining))
    {
        return 0;
    }

    BString command = empty_bstr();
    match_optional(BString, Hundeschule__next_token(&remaining),
        some(token) { command = token; },
        none() { return 0; }
    );

    if (!BString__equals(&command, &hundeschule_driver_command))
    {
        DRIVER_DEBUG("hundeschule: invalid config command=%B line=%B\n", &command, &line);
        return 1;
    }

    BString path = empty_bstr();
    match_optional(BString, Hundeschule__next_token(&remaining),
        some(token) { path = token; },
        none() { return 1; }
    );

    if (BString__is_empty(&path))
    {
        return 1;
    }

    bool respawn = true;
    BString option = empty_bstr();
    match_optional(BString, Hundeschule__next_token(&remaining),
        some(token) { option = token; },
        none_do_nothing()
    );

    if (!BString__is_empty(&option))
    {
        if (BString__equals(&option, &hundeschule_once_option))
        {
            respawn = false;
            option = empty_bstr();
            match_optional(BString, Hundeschule__next_token(&remaining),
                some(token) { option = token; },
                none_do_nothing()
            );
        }
        else if (BString__equals(&option, &hundeschule_respawn_option))
        {
            respawn = true;
            option = empty_bstr();
            match_optional(BString, Hundeschule__next_token(&remaining),
                some(token) { option = token; },
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
        match_optional(BString, Hundeschule__next_token(&remaining),
            some(token) { option = token; },
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

    DRIVER_DEBUG("hundeschule: config spawning path=%B respawn=%i\n", &path, (int) respawn);
    return Hundeschule__spawn_required_process(processes, &path, &hundeschule_empty, &hundeschule_empty, respawn);
}

static int Hundeschule__spawn_configured_drivers(
    HundeschuleRequiredProcessesPtr processes, ConstDynStringPtr config, bool framebuffer_present
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
                        failures += Hundeschule__spawn_configured_driver_line(
                            processes,
                            &bstring_line,
                            framebuffer_present
                        );
                    },
                    none() { failures++; }
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none() { failures++; }
    );

    return failures;
}

static int Hundeschule__spawn_default_drivers(
    HundeschuleRequiredProcessesPtr processes, bool framebuffer_present
)
{
    int failures = 0;
    failures += Hundeschule__spawn_required_process(
        processes,
        &hundeschule_hundebus_path,
        &hundeschule_empty,
        &hundeschule_empty,
        false
    );
    if (framebuffer_present)
    {
        failures += Hundeschule__spawn_required_process(
            processes,
            &hundeschule_virtio_keyboard_path,
            &hundeschule_empty,
            &hundeschule_empty,
            true
        );
    }
    else
    {
        failures += Hundeschule__spawn_required_process(
            processes,
            &hundeschule_uart_rx_path,
            &hundeschule_empty,
            &hundeschule_empty,
            true
        );
        failures += Hundeschule__spawn_required_process(
            processes,
            &hundeschule_uart_tx_path,
            &hundeschule_empty,
            &hundeschule_empty,
            true
        );
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

    failures += Hundeschule__spawn_virtio_block_drivers(processes);

    if (failures != 0)
    {
        return failures;
    }

    failures += Hundeschule__wait_for_bootstrap_block_devices(processes);
    if (failures != 0)
    {
        return failures;
    }

    failures += Hundeschule__spawn_required_process(
        processes,
        &hundeschule_socket_router_path,
        &hundeschule_empty,
        &hundeschule_empty,
        false
    );
    if (failures != 0)
    {
        return failures;
    }

    match_optional(DynString, Hundeschule__load_config(),
        some(config)
        {
            DRIVER_DEBUG("hundeschule: loaded config %s bytes=%i\n", HUNDESCHULE_CONFIG_PATH, (int) DynString__length(&config));
            failures += Hundeschule__spawn_configured_drivers(processes, &config, framebuffer_present);
            DynString__dispose(&config);
        },
        none()
        {
            DRIVER_DEBUG("hundeschule: no config loaded, using defaults\n");
            failures += Hundeschule__spawn_default_drivers(processes, framebuffer_present);
        }
    );

    if (failures != 0)
    {
        return failures;
    }

    failures += Hundeschule__spawn_required_process(
        processes,
        &hundeschule_hundeschau_path,
        &hundeschule_empty,
        &hundeschule_empty,
        false
    );

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
            IO__printf(&bstr("hundeschule: wait child event failed: %i\n"), (int) result);
            return 1;
        }

        match_optional(HundeschuleRequiredProcessPtr, Hundeschule__find_required_process(&required_processes, event.pid),
            some(process)
            {
                if (!process->respawn)
                {
                    continue;
                }

                Optional(BString) process_arg0 = SpawnArg__from_bstring(&process->arg0);
                Optional(BString) process_arg1 = SpawnArg__from_bstring(&process->arg1);
                SpawnResult respawn_result = spawn(
                    P_NOWAIT,
                    &process->path,
                    &process_arg0,
                    &process_arg1,
                    &SPAWN_NO_ARG
                );
                if (respawn_result < 0)
                {
                    IO__printf(&bstr("hundeschule: failed to start %B: %i\n"), &process->path, (int) respawn_result);
                    return 1;
                }

                process->pid = (uint64_t) respawn_result;
            },
            none_do_nothing()
        );
    }
}

#include "../../sys/spawn.h"
#include "../../sys/framebuffer_config.h"
#include "../../sys/io.h"
#include "../../sys/process.h"
#include "../../sys/service.h"
#include "../../user.h"

#ifndef HUNDESCHAU_ENTRYPOINT
#define HUNDESCHAU_ENTRYPOINT main
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
#define HUNDESCHAU_REQUIRED_PROCESSES_MAX 16U
#define HUNDESCHAU_TERMINAL_INPUT ":tty:"
#define HUNDESCHAU_KEYBOARD_INPUT ":keyboard:"
#define HUNDESCHAU_TERMINAL_OUTPUT ":console:"

typedef struct HundeschauRequiredProcess
{
    uint64_t pid;
    bool respawn;
    bool has_stdio;
    SpawnFileDescriptor stdin_fd;
    SpawnFileDescriptor stdout_fd;
    SpawnFileDescriptor stderr_fd;
    const char * path;
    char arg0[32];
    char arg1[SERVICE_DEVICE_PATH_MAX];
} HundeschauRequiredProcess;

typedef struct HundeschauRequiredProcesses
{
    HundeschauRequiredProcess items[HUNDESCHAU_REQUIRED_PROCESSES_MAX];
    uint64_t len;
} HundeschauRequiredProcesses;

static volatile uint32_t * Hundeschau__reg32(void * base, uint64_t offset)
{
    return (volatile uint32_t *) ((uint64_t) base + offset);
}

static uint32_t Hundeschau__read32(void * base, uint64_t offset)
{
    return *Hundeschau__reg32(base, offset);
}

static uint64_t Hundeschau__u64_to_decimal(uint64_t value, char * target, uint64_t size)
{
    char reversed[32];
    uint64_t len = 0;

    if (size == 0)
    {
        return 0;
    }

    if (value == 0)
    {
        if (size > 1)
        {
            target[0] = '0';
            target[1] = '\0';
            return 1;
        }
        target[0] = '\0';
        return 0;
    }

    while (value > 0 && len < sizeof(reversed))
    {
        reversed[len++] = (char) ('0' + (value % 10));
        value /= 10;
    }

    if (len + 1 > size)
    {
        target[0] = '\0';
        return 0;
    }

    for (uint64_t i = 0; i < len; i++)
    {
        target[i] = reversed[len - 1 - i];
    }
    target[len] = '\0';
    return len;
}

static void Hundeschau__copy_string(char * target, uint64_t size, const char * source)
{
    uint64_t i = 0;
    if (source != NULL)
    {
        while (i + 1 < size && source[i] != '\0')
        {
            target[i] = source[i];
            i++;
        }
    }
    target[i] = '\0';
}

static void Hundeschau__virtio_path(uint64_t ordinal, char target[SERVICE_DEVICE_PATH_MAX])
{
    const char * prefix = "/dev/virtio";
    uint64_t i = 0;
    for (; prefix[i] != '\0' && i < SERVICE_DEVICE_PATH_MAX - 1; i++)
    {
        target[i] = prefix[i];
    }

    char suffix[32];
    Hundeschau__u64_to_decimal(ordinal, suffix, sizeof(suffix));
    for (uint64_t j = 0; suffix[j] != '\0' && i < SERVICE_DEVICE_PATH_MAX - 1; j++, i++)
    {
        target[i] = suffix[j];
    }
    target[i] = '\0';
}

static int Hundeschau__spawn_required_process(
    HundeschauRequiredProcesses * processes, const char * path, const char * arg0, const char * arg1, bool respawn
)
{
    SpawnResult result = spawn(P_NOWAIT, path, arg0, arg1, NULL);
    if (result < 0)
    {
        printf("hundeschau: failed to start %s: %i\n", path, (int) result);
        return 1;
    }

    if (processes->len < HUNDESCHAU_REQUIRED_PROCESSES_MAX)
    {
        HundeschauRequiredProcess * process = &processes->items[processes->len++];
        process->pid = (uint64_t) result;
        process->respawn = respawn;
        process->has_stdio = false;
        process->path = path;
        Hundeschau__copy_string(process->arg0, sizeof(process->arg0), arg0);
        Hundeschau__copy_string(process->arg1, sizeof(process->arg1), arg1);
    }

    return 0;
}

static int Hundeschau__spawn_required_process_with_stdio(
    HundeschauRequiredProcesses * processes,
    const char * path,
    SpawnFileDescriptor stdin_fd,
    SpawnFileDescriptor stdout_fd,
    SpawnFileDescriptor stderr_fd,
    bool respawn
)
{
    SpawnResult result = spawn_with_stdio(P_NOWAIT, path, stdin_fd, stdout_fd, stderr_fd, NULL, NULL, NULL);
    if (result < 0)
    {
        printf("hundeschau: failed to start %s: %i\n", path, (int) result);
        return 1;
    }

    if (processes->len < HUNDESCHAU_REQUIRED_PROCESSES_MAX)
    {
        HundeschauRequiredProcess * process = &processes->items[processes->len++];
        process->pid = (uint64_t) result;
        process->respawn = respawn;
        process->has_stdio = true;
        process->stdin_fd = stdin_fd;
        process->stdout_fd = stdout_fd;
        process->stderr_fd = stderr_fd;
        process->path = path;
        Hundeschau__copy_string(process->arg0, sizeof(process->arg0), NULL);
        Hundeschau__copy_string(process->arg1, sizeof(process->arg1), NULL);
    }

    return 0;
}

static int Hundeschau__spawn_login(HundeschauRequiredProcesses * processes, const char * input_path)
{
    IOHandle stdin_fd = open(input_path, IO_OPEN_READ);
    if (stdin_fd < 0)
    {
        printf("hundeschau: failed to open terminal input: %i\n", (int) stdin_fd);
        return 1;
    }

    IOHandle stdout_fd = open(HUNDESCHAU_TERMINAL_OUTPUT, IO_OPEN_WRITE);
    if (stdout_fd < 0)
    {
        printf("hundeschau: failed to open terminal output: %i\n", (int) stdout_fd);
        return 1;
    }

    IOHandle stderr_fd = open(HUNDESCHAU_TERMINAL_OUTPUT, IO_OPEN_WRITE);
    if (stderr_fd < 0)
    {
        printf("hundeschau: failed to open terminal error output: %i\n", (int) stderr_fd);
        return 1;
    }

    return Hundeschau__spawn_required_process_with_stdio(
        processes,
        "/sbin/login",
        (SpawnFileDescriptor) stdin_fd,
        (SpawnFileDescriptor) stdout_fd,
        (SpawnFileDescriptor) stderr_fd,
        true
    );
}

static HundeschauRequiredProcess * Hundeschau__find_required_process(
    HundeschauRequiredProcesses * processes, uint64_t pid
)
{
    for (uint64_t i = 0; i < processes->len; i++)
    {
        if (processes->items[i].pid == pid)
        {
            return &processes->items[i];
        }
    }

    return NULL;
}

static int Hundeschau__spawn_virtio_drivers(HundeschauRequiredProcesses * processes)
{
    ServiceMappedMemoryPtr mapping = 0;
    if (!service_ok(service_map_mmio(VIRTIO_MMIO_FIRST_PHYS_BASE, VIRTIO_MMIO_REGION_SIZE, &mapping)))
    {
        printf("hundeschau: failed to probe virtio mmio\n");
        return 1;
    }

    int failures = 0;
    uint64_t virtio_ordinal = 1;
    for (uint64_t slot = 0; slot < VIRTIO_MMIO_SLOT_COUNT; slot++)
    {
        void * mmio = driver_ptr_offset(mapping, slot * VIRTIO_MMIO_SLOT_SIZE);
        uint32_t magic = Hundeschau__read32(mmio, VIRTIO_MMIO_MAGIC);
        uint32_t version = Hundeschau__read32(mmio, VIRTIO_MMIO_VERSION);
        uint32_t device_id = Hundeschau__read32(mmio, VIRTIO_MMIO_DEVICE_ID);

        if (magic == VIRTIO_MMIO_MAGIC_VALUE && version >= 1 && device_id == VIRTIO_DEVICE_ID_BLOCK)
        {
            char slot_arg[32];
            char path_arg[SERVICE_DEVICE_PATH_MAX];
            Hundeschau__u64_to_decimal(slot, slot_arg, sizeof(slot_arg));
            Hundeschau__virtio_path(virtio_ordinal, path_arg);
            failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/virtio_blk", slot_arg, path_arg, true);
            virtio_ordinal++;
        }
    }

    return failures;
}

static int Hundeschau__initialise_required_processes(HundeschauRequiredProcesses * processes)
{
    int failures = 0;
    FramebufferConfig framebuffer_config = { 0 };
    bool framebuffer_present = FramebufferConfig__load(&framebuffer_config);

    failures += Hundeschau__spawn_virtio_drivers(processes);
    failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/hundebus", NULL, NULL, false);
    if (framebuffer_present)
    {
        failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/video_term", NULL, NULL, false);
        failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/virtio_keyboard", NULL, NULL, true);
        failures += Hundeschau__spawn_login(processes, HUNDESCHAU_KEYBOARD_INPUT);
    }
    else
    {
        failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/uart_rx", NULL, NULL, true);
        failures += Hundeschau__spawn_required_process(processes, "/sbin/drivers/uart_tx", NULL, NULL, true);
        failures += Hundeschau__spawn_login(processes, HUNDESCHAU_TERMINAL_INPUT);
    }

    return failures;
}

int HUNDESCHAU_ENTRYPOINT(void)
{
    HundeschauRequiredProcesses required_processes = { 0 };

    printf("🐶 HundeOS: hundeschau begins 🐾\n");

    if (Hundeschau__initialise_required_processes(&required_processes) != 0)
    {
        return 1;
    }

    while (true)
    {
        ProcessChildEvent event = { 0 };
        ProcessChildEventResult result = wait_for_child_event(&event);
        if (result < 0)
        {
            printf("hundeschau: wait child event failed: %i\n", (int) result);
            return 1;
        }

        HundeschauRequiredProcess * process = Hundeschau__find_required_process(&required_processes, event.pid);
        if (process == NULL)
        {
            continue;
        }
        if (!process->respawn)
        {
            continue;
        }

        SpawnResult respawn_result = process->has_stdio
            ? spawn_with_stdio(
                P_NOWAIT,
                process->path,
                process->stdin_fd,
                process->stdout_fd,
                process->stderr_fd,
                NULL,
                NULL,
                NULL
            )
            : spawn(
                P_NOWAIT,
                process->path,
                process->arg0[0] == '\0' ? NULL : process->arg0,
                process->arg1[0] == '\0' ? NULL : process->arg1,
                NULL
            );
        if (respawn_result < 0)
        {
            printf("hundeschau: failed to start %s: %i\n", process->path, (int) respawn_result);
            return 1;
        }

        process->pid = (uint64_t) respawn_result;
    }
}

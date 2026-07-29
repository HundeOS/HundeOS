#include "../../sys/spawn.h"
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

static int Hundeschau__spawn_background(const char * path, const char * arg0, const char * arg1)
{
    SpawnResult result = spawn(P_NOWAIT, path, arg0, arg1, NULL);
    if (result < 0)
    {
        printf("hundeschau: failed to start %s: %i\n", path, (int) result);
        return 1;
    }
    return 0;
}

static int Hundeschau__spawn_virtio_drivers(void)
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
            failures += Hundeschau__spawn_background("/ebin/virtio_blk", slot_arg, path_arg);
            virtio_ordinal++;
        }
    }

    return failures;
}

int HUNDESCHAU_ENTRYPOINT(void)
{
    int failures = 0;

    printf("🐶 HundeOS: hundeschau begins 🐾\n");

    failures += Hundeschau__spawn_virtio_drivers();
    failures += Hundeschau__spawn_background("/ebin/uart_rx", NULL, NULL);
    failures += Hundeschau__spawn_background("/ebin/uart_tx", NULL, NULL);

    if (failures != 0)
    {
        return 1;
    }

    SpawnResult shell_result = spawn(P_WAIT, "/ebin/shell", NULL, NULL, NULL);
    if (shell_result < 0)
    {
        printf("hundeschau: failed to start /ebin/shell: %i\n", (int) shell_result);
        return 1;
    }

    return 0;
}

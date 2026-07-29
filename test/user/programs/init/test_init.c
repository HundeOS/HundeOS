#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/spawn.h"
#include "../../../../common_includes/syscalls/service.h"

typedef struct SpawnCall
{
    SpawnMode mode;
    char path[64];
    char arg0[64];
    char arg1[64];
}
SpawnCall;

struct ServiceMappedMemory
{
    uint64_t driver_address;
    uint64_t device_address;
    uint64_t size;
};

static SpawnCall spawn_calls[8];
static uint64_t spawn_call_count;
static const char * failing_path;
static char printed_output[256];
static uint64_t printed_output_len;
static uint8_t fake_virtio_region[0x200ULL * 32ULL];
static ServiceMappedMemory fake_mapping;

#define HUNDESCHAU_ENTRYPOINT hundeschau_main_under_test
#define printf test_printf
#include "../../../../user/programs/hundeschau/main.c"
#undef printf

void setUp(void)
{
    spawn_call_count = 0;
    failing_path = NULL;
    printed_output_len = 0;
    for (uint64_t i = 0; i < 8; i++)
    {
        spawn_calls[i] = (SpawnCall) { 0 };
    }
    for (uint64_t i = 0; i < sizeof(printed_output); i++)
    {
        printed_output[i] = '\0';
    }

    fake_mapping = (ServiceMappedMemory) {
        .driver_address = (uint64_t) fake_virtio_region,
        .device_address = 0x0a000000ULL,
        .size = sizeof(fake_virtio_region),
    };

    volatile uint32_t * slot0 = (volatile uint32_t *) &fake_virtio_region[0];
    volatile uint32_t * slot1 = (volatile uint32_t *) &fake_virtio_region[0x200];
    volatile uint32_t * slot2 = (volatile uint32_t *) &fake_virtio_region[0x400];

    slot0[0x000 / 4] = 0x74726976U;
    slot0[0x004 / 4] = 1;
    slot0[0x008 / 4] = 2;

    slot1[0x000 / 4] = 0x74726976U;
    slot1[0x004 / 4] = 1;
    slot1[0x008 / 4] = 0;

    slot2[0x000 / 4] = 0x74726976U;
    slot2[0x004 / 4] = 1;
    slot2[0x008 / 4] = 2;
}

void tearDown(void)
{
}

static int strings_equal(const char * left, const char * right)
{
    uint64_t i = 0;
    while (left[i] != '\0' && right[i] != '\0')
    {
        if (left[i] != right[i])
        {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

static void copy_string(char * target, uint64_t size, const char * source)
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

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2)
{
    TEST_ASSERT_LESS_THAN_UINT64(8, spawn_call_count);
    spawn_calls[spawn_call_count].mode = mode;
    copy_string(spawn_calls[spawn_call_count].path, sizeof(spawn_calls[spawn_call_count].path), path);
    copy_string(spawn_calls[spawn_call_count].arg0, sizeof(spawn_calls[spawn_call_count].arg0), arg0);
    copy_string(spawn_calls[spawn_call_count].arg1, sizeof(spawn_calls[spawn_call_count].arg1), arg1);
    spawn_call_count++;

    if (failing_path != NULL && strings_equal(path, failing_path))
    {
        return SPAWN_NO_FILE;
    }

    return (SpawnResult) spawn_call_count;
}

ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(0x0a000000ULL, physical_address);
    TEST_ASSERT_EQUAL_UINT64(sizeof(fake_virtio_region), size);
    *memory = &fake_mapping;
    return SERVICE_OK;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (void *) (memory->driver_address + offset);
}

int test_printf(const char * format, ...)
{
    uint64_t i = 0;
    while (format[i] != '\0' && printed_output_len + 1 < sizeof(printed_output))
    {
        printed_output[printed_output_len++] = format[i++];
    }
    printed_output[printed_output_len] = '\0';
    return (int) i;
}

void test_init_spawns_drivers_in_background_then_shell_in_foreground(void)
{
    TEST_ASSERT_EQUAL_INT(0, hundeschau_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(5, spawn_call_count);
    TEST_ASSERT_TRUE(strings_equal("🐶 HundeOS: hundeschau begins 🐾\n", printed_output));
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[0].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("0", spawn_calls[0].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", spawn_calls[0].arg1);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[1].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("2", spawn_calls[1].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", spawn_calls[1].arg1);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[2].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_rx", spawn_calls[2].path);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[3].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_tx", spawn_calls[3].path);
    TEST_ASSERT_EQUAL(P_WAIT, spawn_calls[4].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/shell", spawn_calls[4].path);
}

void test_init_does_not_start_shell_when_required_driver_fails(void)
{
    failing_path = "/ebin/virtio_blk";

    TEST_ASSERT_EQUAL_INT(1, hundeschau_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(4, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_rx", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_tx", spawn_calls[3].path);
}

#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/spawn.h"

typedef struct SpawnCall
{
    SpawnMode mode;
    const char * path;
}
SpawnCall;

static SpawnCall spawn_calls[4];
static uint64_t spawn_call_count;
static const char * failing_path;

#define INIT_ENTRYPOINT init_main_under_test
#include "../../../../user/programs/init/main.c"

void setUp(void)
{
    spawn_call_count = 0;
    failing_path = NULL;
    for (uint64_t i = 0; i < 4; i++)
    {
        spawn_calls[i] = (SpawnCall) { 0 };
    }
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

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2)
{
    TEST_ASSERT_LESS_THAN_UINT64(4, spawn_call_count);
    spawn_calls[spawn_call_count++] = (SpawnCall) {
        .mode = mode,
        .path = path,
    };

    if (failing_path != NULL && strings_equal(path, failing_path))
    {
        return SPAWN_NO_FILE;
    }

    return (SpawnResult) spawn_call_count;
}

int printf(const char * format, ...)
{
    return 0;
}

void test_init_spawns_drivers_in_background_then_shell_in_foreground(void)
{
    TEST_ASSERT_EQUAL_INT(0, init_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(4, spawn_call_count);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[0].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[0].path);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[1].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_rx", spawn_calls[1].path);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[2].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_tx", spawn_calls[2].path);
    TEST_ASSERT_EQUAL(P_WAIT, spawn_calls[3].mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/shell", spawn_calls[3].path);
}

void test_init_does_not_start_shell_when_required_driver_fails(void)
{
    failing_path = "/ebin/uart_rx";

    TEST_ASSERT_EQUAL_INT(1, init_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(3, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/ebin/virtio_blk", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_rx", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("/ebin/uart_tx", spawn_calls[2].path);
}

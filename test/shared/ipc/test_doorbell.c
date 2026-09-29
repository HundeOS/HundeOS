#include "unity.h"
#include "ipc/doorbell.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_ring_advances_generation_and_marks_an_observation_changed(void)
{
    IpcDoorbell doorbell = {0};
    ConstIpcEventPtr event = IpcDoorbell__event(&doorbell);
    IpcEventGeneration observed = IpcEvent__generation(event);

    TEST_ASSERT_FALSE(IpcEvent__changed(event, observed));
    IpcEvent__ring(IpcDoorbell__mutable_event(&doorbell));

    TEST_ASSERT_EQUAL_UINT64(observed + 1, IpcEvent__generation(event));
    TEST_ASSERT_TRUE(IpcEvent__changed(event, observed));
}

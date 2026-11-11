#include "unity.h"
#include "hardware/virtio.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_claims_only_supported_dma_devices_with_a_valid_transport(void)
{
    TEST_ASSERT_TRUE(VirtioDevice__claimable(0x74726976U, 1, 1));
    TEST_ASSERT_TRUE(VirtioDevice__claimable(0x74726976U, 1, 2));
    TEST_ASSERT_TRUE(VirtioDevice__claimable(0x74726976U, 2, 18));
    TEST_ASSERT_FALSE(VirtioDevice__claimable(0, 1, 1));
    TEST_ASSERT_FALSE(VirtioDevice__claimable(0x74726976U, 0, 1));
    TEST_ASSERT_FALSE(VirtioDevice__claimable(0x74726976U, 1, 0));
    TEST_ASSERT_FALSE(VirtioDevice__claimable(0x74726976U, 1, 3));
}

#include "unity.h"

#include "early_boot.h"

void setUp(void)
{
}

void tearDown(void)
{
}

static SystemInfo valid_system_info(void)
{
    SystemInfo info = SystemInfo__new();
    info.cpu_count = 1;
    info.physical_memory.physical_start = 0x80000000;
    info.physical_memory.physical_end = 0xa0000000;
    PlicInterruptControllerInfo plic = PlicInterruptControllerInfo__new(0xc000000, 0xc000000, 95, 0);
    info.interrupt_controller = InterruptControllerInfo__new_plic(&plic);
    return info;
}

void test_early_boot_accepts_complete_system_information(void)
{
    KernelEarlyBootInfo info = KernelEarlyBootInfo__new(7, valid_system_info());

    TEST_ASSERT_EQUAL(KERNEL_EARLY_BOOT_READY, KernelEarlyBoot__enter(&info));
}

void test_early_boot_rejects_missing_cpus(void)
{
    SystemInfo system_info = valid_system_info();
    system_info.cpu_count = 0;
    KernelEarlyBootInfo info = KernelEarlyBootInfo__new(0, system_info);

    TEST_ASSERT_EQUAL(KERNEL_EARLY_BOOT_MISSING_CPUS, KernelEarlyBoot__enter(&info));
}

void test_early_boot_rejects_invalid_memory(void)
{
    SystemInfo system_info = valid_system_info();
    system_info.physical_memory.physical_end = system_info.physical_memory.physical_start;
    KernelEarlyBootInfo info = KernelEarlyBootInfo__new(0, system_info);

    TEST_ASSERT_EQUAL(KERNEL_EARLY_BOOT_INVALID_MEMORY, KernelEarlyBoot__enter(&info));
}

void test_early_boot_rejects_a_missing_interrupt_controller(void)
{
    SystemInfo system_info = valid_system_info();
    system_info.interrupt_controller = InterruptControllerInfo__new_none();
    KernelEarlyBootInfo info = KernelEarlyBootInfo__new(0, system_info);

    TEST_ASSERT_EQUAL(KERNEL_EARLY_BOOT_MISSING_INTERRUPT_CONTROLLER, KernelEarlyBoot__enter(&info));
}

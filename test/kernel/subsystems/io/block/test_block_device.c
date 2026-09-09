#include "unity.h"

#include "../../../../../kernel/subsystems/io/block/block_device.h"
#include "../../../stubs/cpu.h"

static KernelContext first_context = EMPTY_KERNEL_CONTEXT;
static KernelContext second_context = EMPTY_KERNEL_CONTEXT;

void setUp(void)
{
    reset_cpu_id();
    first_context = KernelContext__new(&Some(ProcessID, 10), &None(SchedulerPtr));
    second_context = KernelContext__new(&Some(ProcessID, 20), &None(SchedulerPtr));
}

void tearDown(void)
{
    set_cpu_id(1);
    BlockDevice__clear_current_context();
    set_cpu_id(2);
    BlockDevice__clear_current_context();
    reset_cpu_id();
}

void test_block_device_current_context_is_cpu_local(void)
{
    set_cpu_id(1);
    BlockDevice__set_current_context(&first_context);

    set_cpu_id(2);
    BlockDevice__set_current_context(&second_context);
    BlockDevice__clear_current_context();

    set_cpu_id(1);
    match_optional(
        BlockDevice__current_context(),
        some(context) { TEST_ASSERT_EQUAL_PTR(&first_context, context); },
        none_test_fail()
    );
}

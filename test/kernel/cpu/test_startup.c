#include <setjmp.h>

#include "unity.h"
#include "cpu.h"
#include "boot.h"
#include "main.h"
#include "mmu.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "arch/panic.h"
#include "print.h"

static CPUID current_cpu;
static CPUID boot_cpu;
static SystemInfo system_info;
static Kernel kernel;
static unsigned bootstrap_calls;
static unsigned boot_calls;
static unsigned wait_calls;
static unsigned interrupt_wait_calls;
static unsigned halt_calls;
static jmp_buf cpu_escape;

void setUp(void)
{
    current_cpu = 3;
    boot_cpu = 7;
    bootstrap_calls = 0;
    boot_calls = 0;
    wait_calls = 0;
    interrupt_wait_calls = 0;
    halt_calls = 0;
}

void tearDown(void)
{
}

CPUID CPU__id(void)
{
    return current_cpu;
}

bool CPU__is_boot_cpu(void)
{
    return current_cpu == boot_cpu;
}

void CPU__assign_id(CPUID id)
{
    TEST_FAIL_MESSAGE("Kernel entry must preserve the boot CPU identity");
}

void Boot__init_bootstrap_system(SystemInfoPtr info)
{
    TEST_ASSERT_EQUAL_UINT64(boot_cpu, current_cpu);
    TEST_ASSERT_EQUAL_PTR(&system_info, info);
    bootstrap_calls++;
}

void CPU__noop(void)
{
    // Run the primary when the early secondary reaches its wait point.
    TEST_ASSERT_EQUAL_UINT64(3, current_cpu);
    TEST_ASSERT_EQUAL_UINT64(0, CPU__count());
    TEST_ASSERT_EQUAL_UINT(0, bootstrap_calls);
    wait_calls++;
    current_cpu = boot_cpu;
    CPU__enter_kernel(&system_info);
    current_cpu = 3;
}

void CPU__wait_for_interrupt(void)
{
    interrupt_wait_calls++;
    longjmp(cpu_escape, 1);
}

__attribute__((noreturn)) void CPU__halt_forever(void)
{
    halt_calls++;
    longjmp(cpu_escape, 1);
}

KernelPtr Kernel__instance(void)
{
    TEST_ASSERT_EQUAL_UINT(1, bootstrap_calls);
    return &kernel;
}

void MMU__install_null_low_mem_table(KernelPtr self)
{
    TEST_ASSERT_EQUAL_PTR(&kernel, self);
    TEST_ASSERT_EQUAL_UINT(1, bootstrap_calls);
}

void Kernel__boot(void)
{
    TEST_ASSERT_EQUAL_UINT(1, bootstrap_calls);
    boot_calls++;
}

void Kernel__halt(void)
{
}

int k_printf(const char * format, ...)
{
    return 0;
}

void test_out_of_order_cpus_preserve_identity_and_wait_for_primary_bootstrap(void)
{
    CPU__enter_kernel(&system_info);
    TEST_ASSERT_EQUAL_UINT64(3, current_cpu);
    TEST_ASSERT_EQUAL_UINT64(2, CPU__count());
    TEST_ASSERT_EQUAL_UINT(1, wait_calls);
    current_cpu = 4;
    CPU__enter_kernel(&system_info);
    TEST_ASSERT_EQUAL_UINT64(4, current_cpu);
    current_cpu = 2;
    CPU__enter_kernel(&system_info);
    TEST_ASSERT_EQUAL_UINT64(2, current_cpu);
    TEST_ASSERT_EQUAL_UINT64(4, CPU__count());
    TEST_ASSERT_EQUAL_UINT(1, bootstrap_calls);
    TEST_ASSERT_EQUAL_UINT(1, wait_calls);
    TEST_ASSERT_EQUAL_UINT(4, boot_calls);
}

void test_shared_wait_and_halt_policy_uses_architecture_primitives(void)
{
    if (setjmp(cpu_escape) == 0)
    {
        CPU__wait();
    }
    TEST_ASSERT_EQUAL_UINT(1, interrupt_wait_calls);

    CPU__check_halt();
    TEST_ASSERT_EQUAL_UINT(0, halt_calls);

    if (setjmp(cpu_escape) == 0)
    {
        CPU__halt_all();
    }
    TEST_ASSERT_EQUAL_UINT(1, halt_calls);

    if (setjmp(cpu_escape) == 0)
    {
        CPU__check_halt();
    }
    TEST_ASSERT_EQUAL_UINT(2, halt_calls);
}

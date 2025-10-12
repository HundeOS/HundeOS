#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"

#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"
#include "../stubs/process.h"
#include "../stubs/cpu.h"

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
    reset_started_processes();
    reset_cpu_id();
    set_cpu_id(100);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_spawning_from_elf(void)
{
    printf("here\n");
    Elf * elf = EmbeddedElf__load("hello_world");
    printf("here\n");
    Process * process = Process__spawn_from_elf(elf);
    printf("here\n");
    TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
        0x400000,
        true,
        false,
        true,
        101));
    TEST_ASSERT(check_address_mapped(v_ptr(process->page_table, PageTable),
        BASE_STACK,
        true,
        true,
        false,
        STACK_SIZE));

    TEST_ASSERT(process_is_started(process));
    // TEST_ASSERT(process_tied_to_cpu(100, process));
}
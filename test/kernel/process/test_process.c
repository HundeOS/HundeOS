#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"

#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_setup_from_elf(void)
{
    ElfPtr elf = EmbeddedElf__load("hello_world");
    ResultMatch(ProcessPtr, Process__setup(elf))
        IsError(code, message,
        {
            TEST_FAIL();
        })
        IsSuccess(process,
        {
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
        })
    End
}
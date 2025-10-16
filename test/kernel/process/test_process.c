#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../../../kernel/print.h"

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
    set_cpu_id(5);
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

bool is_process_in_table(ProcessPtr process, ProcessTablePtr table)
{
    for (int i = 0; i < table->len; i++)
    {
        Match(ProcessPtr, ProcessTable__at(table, i))
            IsSome(proc,
                {
                    return proc == process;
                }
            );
        IsNone(return false);
        End;
    }
}

bool process_tied_to_cpu(int cpu, Process * process)
{
    Optional(ProcessPtr) opt_process = Process__current_for_cpu(cpu);
    Match(ProcessPtr, opt_process);
        IsSome(cpu_proc,
            {
                return cpu_proc == process;
            }
        );
        IsNone(return false);
    End;
    return false;
}

bool process_in_cpu_processes(int cpu, ProcessPtr process)
{
    Optional(ProcessTablePtr) opt_process = Process__all_for_cpu(cpu);
    Match(ProcessTablePtr, opt_process);
        IsSome(process_table_ptr,
            {
                return is_process_in_table(process, process_table_ptr);
            }
        );
        IsNone(return false);
    End;
    return false;
}

void test_spawning_from_elf(void)
{
    Elf * elf = EmbeddedElf__load("hello_world");
    Process * process = Process__spawn_from_elf(elf);

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
    TEST_ASSERT(process_tied_to_cpu(5, process));
    TEST_ASSERT(process_in_cpu_processes(5, process));
}
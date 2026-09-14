#include "unity.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../../../kernel/service/process_registry.h"

static KernelPtr kernel;
static ProcessPtr process;

void setUp(void)
{
    kernel = init_test_kernel_without_filesystem();
    process = dummy_process(20, 0, PROC_STATE_READY);
}
void tearDown(void)
{
    KAllocStub__set_user_alloc_failure(false);
}

void test_process_registry_is_shared_read_only_non_executable_memory(void)
{
    ProcessPtr peer = dummy_process(21, 0, PROC_STATE_READY);
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), PROCESS_REGISTRY_VADDR,
                                          true, false, false, PROCESS_REGISTRY_SIZE));
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&peer->page_table), PROCESS_REGISTRY_VADDR, true,
                                          false, false, PROCESS_REGISTRY_SIZE));
    TEST_ASSERT_FALSE(kernel->process_registry.mapping.write);
    TEST_ASSERT_FALSE(kernel->process_registry.mapping.execute);
    ProcessRecord record = ProcessRecord__new(process, 0);
    match_optional(
        record.memory,
        some(usage) { TEST_ASSERT_GREATER_OR_EQUAL_UINT64(PROCESS_REGISTRY_SIZE, usage.shared_bytes); },
        none_test_fail()
    );
}

void test_process_registry_allocation_failure_does_not_publish_mapping(void)
{
    KernelProcessRegistry other = KernelProcessRegistry__new();
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        KernelProcessRegistry__map_process(&other, process),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_MAP_FAILED, code); }
    );
    TEST_ASSERT_FALSE(other.mapping.valid);
}

void test_scheduler_publishes_new_process_with_assigned_cpu_and_typed_state(void)
{
    process->name = bstr("published");
    process->user_id = 2;
    process->group_id = 2;
    match_result(Scheduler__schedule_on_cpu(kernel->scheduler, process, 2),
        success_do_nothing(),
        error_test_fail()
    );
    ProcessRegistryPtr registry = k_v_ptr(kernel->process_registry.mapping, ProcessRegistry);
    match_result(
        ProcessRegistry__find(registry, process->pid),
        success(found)
        {
            match_optional(
                found,
                some(record)
                {
                    TEST_ASSERT_EQUAL(2, record.cpu);
                    TEST_ASSERT_EQUAL(PROC_STATE_READY, record.state);
                    TEST_ASSERT_EQUAL(2, record.credentials.user_id);
                    TEST_ASSERT_EQUAL_BSTRING(bstr("published"), record.name);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    KernelProcessRegistry__set_count(&kernel->process_registry, 2, 0);
    match_result(
        ProcessRegistry__find(registry, process->pid),
        success(found) { match_optional(found,
            some(record) { TEST_FAIL(); },
            none_do_nothing()
        ); },
        error_test_fail()
    );
}

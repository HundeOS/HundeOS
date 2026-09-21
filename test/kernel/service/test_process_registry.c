#include "unity.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../../../kernel/service/process_registry.h"
#include "../../../kernel/scheduler/shared_wait.h"
#include "ipc/queue_wait_control.h"

static KernelPtr kernel;
static ProcessPtr process;

void setUp(void)
{
    kernel = init_test_kernel();
    process = dummy_process(20, 0, PROC_STATE_READY);
}
void tearDown(void)
{
    KAllocStub__set_user_alloc_failure(false);
}

void test_process_record_samples_advisory_ipc_counters_from_owned_wait_mapping(void)
{
    ProcessRecord initial = ProcessRecord__new(process, 0);
    match_optional(initial.io_diagnostics,
        some_test_fail(),
        none_do_nothing()
    );
    match_result(Process__queue_wait_control(process),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        process->queue_wait_control,
        some(mapping)
        {
            QueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
            IODiagnostics__attach(&control->diagnostics);
            IODiagnostics__record(IO_DIAGNOSTIC_UART_EMPTY, 19);
            ProcessRecord sampled = ProcessRecord__new(process, 0);
            match_optional(
                sampled.io_diagnostics,
                some(totals)
                {
                    TEST_ASSERT_EQUAL_UINT64(19, totals.uart_empty);
                    TEST_ASSERT_EQUAL_UINT64(0, totals.uart_bytes);
                },
                none_test_fail()
            );
        },
        none_test_fail()
    );
}

void test_process_record_formats_user_wait_duration_without_changing_scheduler_state(void)
{
    ProcessRecord record = {
        .pid = process->pid,
        .state = PROC_STATE_INTERRUPTED,
        .wait_reason = None(ParkReason),
        .user_wait = Some(ProcessUserWait, ((ProcessUserWait){.started_at_usec = 10, .elapsed_usec = 1234567})),
        .user_wait_usec = 7654321,
        .name = bstr("waiting")};

    ProcessInfo info = ProcessRecord__info(&record);

    TEST_ASSERT_EQUAL_STRING("PROC_STATE_INTERRUPTED", info.state);
    TEST_ASSERT_EQUAL_STRING("USER_WAIT 1.234s", info.wait_reason);
    TEST_ASSERT_EQUAL_UINT64(7654321, info.user_wait_usec);
}

void test_process_record_includes_completed_userspace_wait_time_when_not_currently_waiting(void)
{
    unwrap_result(Process__queue_wait_control(process), 0);
    QueueWaitSnapshot observed = {.kind = QUEUE_WAIT_CONNECTION, .handle = 9};
    match_optional(
        process->queue_wait_control,
        some(mapping)
        {
            QueueWaitControlPtr control = k_v_ptr(mapping, QueueWaitControl);
            QueueWaitControl__begin(control);
            unwrap_result(QueueWaitControl__add(control, &observed), ATOMIC_RECORD_INVALID);
            QueueWaitControl__commit(control, 1000, QUEUE_WAIT_NO_DEADLINE, QueueWaitControl__wake_generation(control));
            QueueWaitControl__complete_wait(control, 6000);

            ProcessRecord record = ProcessRecord__new(process, 0);

            TEST_ASSERT_EQUAL_UINT64(5000, record.user_wait_usec);
            match_optional(record.user_wait,
                some_test_fail(),
                none_do_nothing()
            );
        },
        none_test_fail()
    );
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
        some(usage)
        {
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64(PROCESS_REGISTRY_SIZE, usage.shared_bytes);
        },
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
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SERVICE_MAP_FAILED, code);
        }
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
        success(found)
        {
            match_optional(found,
            some(record)
            {
                TEST_FAIL();
            },
            none_do_nothing()
        );
        },
        error_test_fail()
    );
}

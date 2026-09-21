#include "unity.h"

#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/types/optional.h"

#include "../../../kernel/exec/exec.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/cpu.h"
#include "../stubs/kalloc.h"
#include "../stubs/process.h"
#include "../../../kernel/types/types.h"

static KernelPtr kernel;
static ProcessPtr parent;

static BString exec_path = bstr("/sbin/hello_world");
static SpawnRequest exec_request;

static OriginatingContext context;

static const CPUID cpu = 0;

static ProcessContextPtr no_process_context(void)
{
    return NULL; // hundelint: ignore-no-null
}

static size_t test_hello_world_elf_size(void)
{
    BString name = bstring("hello_world");
    match_optional(
        EmbeddedElf__load(&name),
        some(elf)
        {
            return elf.end - elf.start;
        },
        none()
        {
            TEST_FAIL_MESSAGE("hello_world ELF not found");
        }
    );

    return 0;
}

static SpawnRequest test_spawn_request(ConstBStringPtr path)
{
    SpawnArguments arguments = SpawnArguments__new();
    return SpawnRequest__new(P_WAIT, path, SPAWN_INHERIT_USER_ID, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT,
                             &arguments);
}

void setUp(void)
{
    KAllocStub__reset_allocation_tracking();
    reset_cpu_count();
    kernel = init_test_kernel();
    parent = dummy_process(1, cpu, PROC_STATE_RUNNING);
    kernel->boot_authority = parent->pid;

    exec_request = test_spawn_request(&exec_path);
    Optional(ProcessPtr) process = Some(ProcessPtr, parent);
    CallContext syscall_context = CallContext__new(&process, SYSCALL_EXEC, (Anything)&exec_request, 0, 0, 0, 0);

    context = OriginatingContext__syscall(kernel, &syscall_context);

    Scheduler__schedule(kernel->scheduler, parent);
    Scheduler__preempt(kernel->scheduler, cpu, no_process_context());
}

void tearDown(void)
{
    reset_cpu_count();
}

void test_embedded_exec_does_not_require_a_kernel_filesystem(void)
{
    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &exec_path, false, &arguments),
        success(process)
        {
            TEST_ASSERT_EQUAL_BSTRING(process->name, exec_path);
        },
        error_test_fail()
    );
}

void test_exec_spawns_process_and_puts_parent_in_wait()
{
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
    TEST_ASSERT_EQUAL(1, Scheduler__process_count(kernel->scheduler));
    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &exec_path, true, &arguments),
        success(spawned_process)
        {
            ProcessPtr spawned_parent = unwrap_optional(spawned_process->parent, parent);
            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
            TEST_ASSERT_EQUAL(parent, spawned_parent);
            TEST_ASSERT_EQUAL(2, Scheduler__process_count(kernel->scheduler));
        },
        error_test_fail()
    );
}

void test_exec_does_not_copy_full_elf_past_loaded_segment_buffer(void)
{
    TEST_ASSERT_FALSE(KAllocStub__memcopy_overflow_occurred());

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &exec_path, false, &arguments),
        success(spawned_process)
        {
            TEST_ASSERT_NOT_NULL(spawned_process);
            TEST_ASSERT_FALSE(KAllocStub__memcopy_overflow_occurred());
        },
        error_test_fail()
    );
}

void test_exec_frees_temporary_elf_read_buffer(void)
{
    size_t elf_size = test_hello_world_elf_size();
    size_t frees_before = KAllocStub__freed_allocation_count_for_size(elf_size);

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &exec_path, false, &arguments),
        success(spawned_process)
        {
            TEST_ASSERT_NOT_NULL(spawned_process);
            TEST_ASSERT_EQUAL(frees_before + 1, KAllocStub__freed_allocation_count_for_size(elf_size));
        },
        error_test_fail()
    );
}

void test_exec_spawns_process_and_sets_up_a_defferred_return()
{
    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
    TEST_ASSERT_EQUAL(1, Scheduler__process_count(kernel->scheduler));
    ReturnCode return_code = 55;

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &exec_path, true, &arguments),
        success(spawned_process)
        {
            Scheduler__exit(kernel->scheduler, spawned_process, return_code);
            Scheduler__preempt(kernel->scheduler, cpu, no_process_context());

            TEST_ASSERT_EQUAL(return_code, unwrap_result(deferred_syscall_result(), 0));
        },
        error_test_fail()
    );
}

void test_exec_wait_schedules_child_on_parent_cpu(void)
{
    set_cpu_count(2);
    Scheduler__start(kernel->scheduler, 1);

    ProcessPtr cpu1_parent = dummy_process(1, 1, PROC_STATE_RUNNING);
    kernel->boot_authority = cpu1_parent->pid;
    SpawnRequest request = test_spawn_request(&bstr("/sbin/hello_world"));
    CallContext syscall_context =
        CallContext__new(&Some(ProcessPtr, cpu1_parent), SYSCALL_EXEC, (Anything)&request, 0, 0, 0, 0);
    OriginatingContext cpu1_context = OriginatingContext__syscall(kernel, &syscall_context);

    Scheduler__schedule(kernel->scheduler, cpu1_parent);
    Scheduler__preempt(kernel->scheduler, 1, no_process_context());

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&cpu1_context, &bstr("/sbin/hello_world"), true, &arguments),
        success(spawned_process)
        {
            ProcessTablePtr cpu1_processes = Scheduler__all_processes(kernel->scheduler, 1);
            match_optional(ProcessTable__find_item(cpu1_processes, spawned_process),
                some_do_nothing(),
                           none_test_fail()
            );

            TEST_ASSERT_EQUAL(PROC_STATE_WAITING, cpu1_parent->state);
        },
        error_test_fail()
    );
}

void test_exec_marks_hundeschau_as_hund_identity(void)
{
    Process__set_hund_identity(parent);
    SpawnRequest request = test_spawn_request(&bstr("/sbin/services/hundeschau"));
    CallContext syscall_context =
        CallContext__new(&Some(ProcessPtr, parent), SYSCALL_EXEC, (Anything)&request, 0, 0, 0, 0);
    OriginatingContext hundeschau_context = OriginatingContext__syscall(kernel, &syscall_context);

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&hundeschau_context, &bstr("/sbin/services/hundeschau"), false, &arguments),
        success(spawned_process)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, spawned_process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, spawned_process->group_id);
        },
        error_test_fail()
    );
}

void test_exec_apply_builtin_identity_policy_marks_boot_hundeschau_as_hund(void)
{
    ProcessPtr process = dummy_process(0, cpu, PROC_STATE_NEW);
    process->name = bstr("services/hundeschau");

    Exec__apply_builtin_identity_policy(process);

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, process->group_id);
}

void test_exec_apply_builtin_identity_policy_marks_boot_hundeschule_as_hund(void)
{
    ProcessPtr process = dummy_process(0, cpu, PROC_STATE_NEW);
    process->name = bstr("services/hundeschule");

    Exec__apply_builtin_identity_policy(process);

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, process->group_id);
}

void test_exec_apply_builtin_identity_policy_marks_sdhci_as_hund(void)
{
    ProcessPtr process = dummy_process(0, cpu, PROC_STATE_NEW);
    process->name = bstr("/sbin/drivers/sdhci_channelio");

    Exec__apply_builtin_identity_policy(process);

    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, process->group_id);
}

void test_exec_apply_builtin_identity_policy_marks_mama_as_hund(void)
{
    ProcessPtr process = dummy_process(0, cpu, PROC_STATE_NEW);
    process->name = bstr("services/hunde-mama");
    Exec__apply_builtin_identity_policy(process);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, process->group_id);
    process->name = bstr("/sbin/services/hunde-mama");
    Exec__apply_builtin_identity_policy(process);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, process->user_id);
}

void test_exec_as_can_spawn_shell_as_user(void)
{
    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec_as(&context, &bstr("/sbin/shell"), false, &Some(UserID, PROCESS_USER_USER), &arguments),
        success(spawned_process)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_USER, spawned_process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_USER, spawned_process->group_id);
        },
        error_test_fail()
    );
}

void test_exec_as_denies_unprivileged_identity_switch(void)
{
    Process__set_user_identity(parent);
    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec_as(&context, &bstr("/sbin/shell"), false, &Some(UserID, PROCESS_USER_HUND), &arguments),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
}

void test_exec_denies_service_path_to_user_process(void)
{
    Process__set_user_identity(parent);
    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &bstr("/sbin/services/humpen"), false, &arguments),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
}

void test_exec_allows_service_path_to_hund_process(void)
{
    Process__set_hund_identity(parent);

    ProcessArguments arguments = ProcessArguments__new();
    match_result(
        Exec__exec(&context, &bstr("/sbin/services/humpen"), false, &arguments),
        success(spawned_process)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, spawned_process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, spawned_process->group_id);
        },
        error_test_fail()
    );
}

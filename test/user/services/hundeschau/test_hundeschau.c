#include <stdint.h>

#include "unity.h"

#define HUNDESCHAU_ENTRYPOINT hundeschau_main_under_test
#define IO__printf test_printf
#include "../../../../user/services/hundeschau/main.c"
#undef IO__printf

static uint64_t spawn_count;
static BString spawned_path;

void setUp(void)
{
    spawn_count = 0;
    spawned_path = empty_bstr();
}

void tearDown(void)
{
}

SpawnResult spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                  OptionalPtr(BString) arg2)
{
    TEST_ASSERT_EQUAL(P_NOWAIT, mode);
    spawned_path = *path;
    return ++spawn_count;
}

SpawnResult spawn_with_stdio(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                             SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd, OptionalPtr(BString) arg0,
                             OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_FAIL_MESSAGE("Unexpected stdio spawn");
    return SPAWN_FAILED;
}

IOHandle open(ConstBStringPtr path, Flags flags)
{
    TEST_FAIL_MESSAGE("Unexpected file open");
    return IO_INVALID_PATH;
}

IOStatus close(IOHandle handle)
{
    TEST_FAIL_MESSAGE("Unexpected file close");
    return IO_OK;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    TEST_FAIL_MESSAGE("Unexpected file read");
    return None(DynString);
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    TEST_FAIL_MESSAGE("Unexpected child event wait");
    return PROCESS_CHILD_EVENT_QUEUE_EMPTY;
}

int test_printf(ConstBStringPtr format, ...)
{
    return 0;
}

void test_hundeschau_configured_driver_uses_final_token_without_delimiter(void)
{
    HundeschauRequiredProcesses processes = HundeschauRequiredProcesses__new();
    TEST_ASSERT_EQUAL(0, Hundeschau__spawn_configured_driver_line(
                             &processes, &bstr(" \tdriver \t/sbin/example\trespawn always"), false));
    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
    TEST_ASSERT_EQUAL_BSTRING(spawned_path, bstr("/sbin/example"));
    match_optional(
        Hundeschau__find_required_process(&processes, 1),
        some(process) { TEST_ASSERT_TRUE(process->respawn); },
        none_test_fail()
    );
    TEST_ASSERT_EQUAL(1, Hundeschau__spawn_configured_driver_line(
                             &processes, &bstr("driver /sbin/example respawn always unexpected"), false));
    TEST_ASSERT_EQUAL_UINT64(1, spawn_count);
}

void test_hundeschau_lookup_handles_missing_pid_and_updates_stored_record(void)
{
    HundeschauRequiredProcesses processes = HundeschauRequiredProcesses__new();
    match_optional(Hundeschau__find_required_process(&processes, 42),
        some_test_fail(),
        none_do_nothing()
    );
    HundeschauRequiredProcess first =
        HundeschauRequiredProcess__new(11, true, &bstr("/first"), &empty_bstr(), &empty_bstr());
    HundeschauRequiredProcess last =
        HundeschauRequiredProcess__new(42, true, &bstr("/last"), &empty_bstr(), &empty_bstr());
    match_result(HundeschauRequiredProcesses__push(&processes, first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(HundeschauRequiredProcesses__push(&processes, last),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(Hundeschau__find_required_process(&processes, 99),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        Hundeschau__find_required_process(&processes, 42),
        some(process)
        {
            TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/last"));
            process->pid = 84;
        },
        none_test_fail()
    );
    match_optional(Hundeschau__find_required_process(&processes, 42),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        Hundeschau__find_required_process(&processes, 84),
        some(process) { TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/last")); },
        none_test_fail()
    );
    match_optional(
        Hundeschau__find_required_process(&processes, 11),
        some(process) { TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/first")); },
        none_test_fail()
    );
}

void test_hundeschau_next_token_handles_whitespace_and_exhaustion(void)
{
    BString remaining = bstr(" \tdriver \t /sbin/demo  once \r ");
    match_optional(
        Hundeschau__next_token(&remaining),
        some(token) { TEST_ASSERT_EQUAL_BSTRING(token, bstr("driver")); },
        none_test_fail()
    );
    match_optional(
        Hundeschau__next_token(&remaining),
        some(token) { TEST_ASSERT_EQUAL_BSTRING(token, bstr("/sbin/demo")); },
        none_test_fail()
    );
    match_optional(
        Hundeschau__next_token(&remaining),
        some(token) { TEST_ASSERT_EQUAL_BSTRING(token, bstr("once")); },
        none_test_fail()
    );
    match_optional(Hundeschau__next_token(&remaining),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_TRUE(BString__is_empty(&remaining));
    remaining = bstr(" \t\r ");
    match_optional(Hundeschau__next_token(&remaining),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_TRUE(BString__is_empty(&remaining));
}

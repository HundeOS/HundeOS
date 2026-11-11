#include "unity.h"
#include "../../../kernel/scheduler/scheduler.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/syscall/deferred.h"
#include "../../../kernel/syscall/process_handler.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/cpu.h"
#include "../stubs/panic.h"
#include "../stubs/kalloc.h"
#include "../stubs/thread_context_layout.h"

static KernelPtr kernel;
static SchedulerPtr scheduler;
static Tick next_process;

void setUp(void)
{
    clear_panic_state();
    set_cpu_id(0);
    reset_cpu_count();
    kernel = init_test_kernel();
    scheduler = kernel->scheduler;
    next_process = 1;
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

static ProcessPtr ThreadWaitFixture__process(void)
{
    ProcessPtr process = dummy_process(next_process++, 0, PROC_STATE_READY);
    match_result(Scheduler__schedule_on_cpu(scheduler, process, 0),
        success_do_nothing(),
        error_test_fail()
    );
    return process;
}

static UserAddress ThreadWaitFixture__word(ProcessPtr process)
{
    UserAddress allocation = unwrap_result(Process__alloc_user_pages(process, PAGE_SIZE), NULL_USER_ADDRESS);
    ThreadWaitValuePtr word = k_v_ptr(allocation, ThreadWaitValue);
    __atomic_store_n(word, 7, __ATOMIC_RELEASE);
    return allocation;
}

static void ThreadWaitFixture__park(ThreadPtr thread, Address address, ThreadWaitTimeout timeout)
{
    match_result(
        Scheduler__wait_atomic(scheduler, thread, address, 7, timeout),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SYSCALL_IO_BLOCK, code);
        }
    );
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, thread->state);
}

static ThreadWaitStatus ThreadWaitFixture__result(ThreadPtr thread)
{
    match_optional(
        ParkedOperations__at(&thread->parked_operations, -1),
        some(operation)
        {
            TEST_ASSERT_TRUE(ParkedOperation__is_ready(operation));
            return unwrap_optional(operation->return_code, THREAD_WAIT_FAILED);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Missing atomic wait operation");
        }
    );
    return THREAD_WAIT_FAILED;
}

void test_wake_before_wait_returns_changed_without_parking(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    __atomic_store_n(k_v_ptr(word, ThreadWaitValue), 8, __ATOMIC_RELEASE);
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    TEST_ASSERT_EQUAL(
        THREAD_WAIT_CHANGED,
        unwrap_result(Scheduler__wait_atomic(scheduler, &process->thread, word.virtual_addr, 7, THREAD_WAIT_INDEFINITE),
                      THREAD_WAIT_FAILED));
    TEST_ASSERT_EQUAL(0, ParkedOperations__len(&process->thread.parked_operations));
    TEST_ASSERT_EQUAL(PROC_STATE_READY, process->thread.state);
}

void test_wait_rejects_unmapped_misaligned_read_only_and_nonprivate_words(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, 0)));
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, word.virtual_addr + 1)));
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, word.virtual_addr + PAGE_SIZE)));
    match_result(Process__protect_user_pages(process, word.virtual_addr, PAGE_SIZE, MEM_PAGE_READ_ONLY),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, word.virtual_addr)));
    UserAddress mmio = UserAddress__new(0x50000000, KERNEL_ADDR(0x60000000, 0x60000000), true, true, false, true);
    match_result(Process__record_user_allocation(process, mmio, PAGE_SIZE, USER_ALLOCATION_MMIO),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, mmio.virtual_addr)));
    UserAddress shared = UserAddress__new(0x50001000, KERNEL_ADDR(0x60001000, 0x60001000), true, true, false, true);
    match_result(Process__record_user_allocation(process, shared, PAGE_SIZE, USER_ALLOCATION_SHARED),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Process__atomic_wait_mapping(process, shared.virtual_addr)));
    match_result(
        Scheduler__wait_atomic(scheduler, &process->thread, 0, 7, THREAD_WAIT_INDEFINITE),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, code);
        }
    );
    TEST_ASSERT_EQUAL(0, ParkedOperations__len(&process->thread.parked_operations));
}

void test_wait_zero_timeout_and_expiry_have_distinct_results(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    TEST_ASSERT_EQUAL(THREAD_WAIT_TIMED_OUT,
                      unwrap_result(Scheduler__wait_atomic(scheduler, &process->thread, word.virtual_addr, 7, 0),
                                    THREAD_WAIT_FAILED));
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, 1000);
    TEST_ASSERT_FALSE(Thread__wake_expired_sleep(&process->thread, 1));
    TEST_ASSERT_TRUE(Thread__wake_expired_sleep(&process->thread, 2));
    TEST_ASSERT_EQUAL(THREAD_WAIT_TIMED_OUT, ThreadWaitFixture__result(&process->thread));
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
}

void test_wake_wins_timeout_and_is_not_counted_twice(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, 1000);
    TEST_ASSERT_EQUAL(1, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, process->thread.state);
    TEST_ASSERT_FALSE(Thread__wake_expired_sleep(&process->thread, 100));
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, ThreadWaitFixture__result(&process->thread));
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
}

void test_indefinite_wait_does_not_expire(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, THREAD_WAIT_INDEFINITE);
    TEST_ASSERT_FALSE(Thread__wake_expired_sleep(&process->thread, UINT64_MAX));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->thread.state);
}

void test_large_finite_timeout_does_not_overflow_rounding_into_an_immediate_expiry(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, THREAD_WAIT_INDEFINITE - 1);
    TEST_ASSERT_FALSE(Thread__wake_expired_sleep(&process->thread, 2));
    TEST_ASSERT_TRUE(Thread__wake_expired_sleep(&process->thread, UINT64_MAX));
    TEST_ASSERT_EQUAL(THREAD_WAIT_TIMED_OUT, ThreadWaitFixture__result(&process->thread));
}

void test_wake_is_process_scoped_even_with_identical_virtual_addresses(void)
{
    ProcessPtr first = ThreadWaitFixture__process();
    ProcessPtr second = ThreadWaitFixture__process();
    UserAddress first_word = ThreadWaitFixture__word(first);
    UserAddress second_word = ThreadWaitFixture__word(second);
    TEST_ASSERT_EQUAL_HEX64(first_word.virtual_addr, second_word.virtual_addr);
    ThreadWaitFixture__park(&first->thread, first_word.virtual_addr, THREAD_WAIT_INDEFINITE);
    ThreadWaitFixture__park(&second->thread, second_word.virtual_addr, THREAD_WAIT_INDEFINITE);
    TEST_ASSERT_EQUAL(1,
                      unwrap_result(Scheduler__wake_atomic(scheduler, &first->thread, first_word.virtual_addr, 1), -1));
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, ThreadWaitFixture__result(&first->thread));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, second->thread.state);
}

void test_wake_count_and_address_select_only_matching_waiters(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    UserAddress other_word = ThreadWaitFixture__word(process);
    Address entry = process->image.entry_point.virtual_addr;
    match_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0),
        success_do_nothing(),
                 error_test_fail()
    );
    ThreadPtr second = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    ThreadPtr third = unwrap_optional(ProcessThreads__at(&process->threads, 2), (ThreadPtr){});
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, THREAD_WAIT_INDEFINITE);
    ThreadWaitFixture__park(second, word.virtual_addr, THREAD_WAIT_INDEFINITE);
    ThreadWaitFixture__park(third, other_word.virtual_addr, THREAD_WAIT_INDEFINITE);
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 0), -1));
    TEST_ASSERT_EQUAL(1, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, second->state);
    TEST_ASSERT_EQUAL(
        1, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, UINT64_MAX), -1));
    TEST_ASSERT_EQUAL(PROC_STATE_INTERRUPTED, second->state);
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, third->state);
}

void test_wait_does_not_hide_pending_signal_or_restart_after_handler(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ProcessSignalAction action = {.handler = 0x1000, .restorer = 0x2000, .flags = PROCESS_SIGNAL_ACTION_RESTART};
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_USER_1, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_USER_1);
    TEST_ASSERT_EQUAL(
        THREAD_WAIT_INTERRUPTED,
        unwrap_result(Scheduler__wait_atomic(scheduler, &process->thread, word.virtual_addr, 7, THREAD_WAIT_INDEFINITE),
                      THREAD_WAIT_FAILED));
    ThreadContext context = {.elr = 0x3000};
    match_result(Thread__deliver_pending_signal(&process->thread, &context),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Thread__restore_signal_context(&process->thread),
        success_do_nothing(),
        error_test_fail()
    );
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, THREAD_WAIT_INDEFINITE);
    Process__queue_signal(process, PROCESS_SIGNAL_USER_1);
    match_result(
        Thread__deliver_pending_signal(&process->thread, &context),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_HANDLER, delivery.kind);
            match_optional(
                delivery.interrupted_operation,
                some(operation)
                {
                    ParkedOperation__dispose(operation);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, ParkedOperations__len(&process->thread.parked_operations));
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    match_result(Thread__restore_signal_context(&process->thread),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(THREAD_WAIT_INTERRUPTED, (int64_t)Thread__required_context(&process->thread)->x[0]);
}

void test_stopped_waiter_records_wake_without_becoming_runnable(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, 1000);
    process->thread.state = PROC_STATE_STOPPED;
    TEST_ASSERT_EQUAL(1, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    TEST_ASSERT_EQUAL(PROC_STATE_STOPPED, process->thread.state);
    TEST_ASSERT_EQUAL(THREAD_WAIT_WOKEN, ThreadWaitFixture__result(&process->thread));
    TEST_ASSERT_FALSE(Thread__wake_expired_sleep(&process->thread, 100));
}

void test_stopped_waiter_can_expire_without_becoming_runnable(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    ThreadWaitFixture__park(&process->thread, word.virtual_addr, 1000);
    process->thread.state = PROC_STATE_STOPPED;
    TEST_ASSERT_TRUE(Thread__wake_expired_sleep(&process->thread, 2));
    TEST_ASSERT_EQUAL(PROC_STATE_STOPPED, process->thread.state);
    TEST_ASSERT_EQUAL(THREAD_WAIT_TIMED_OUT, ThreadWaitFixture__result(&process->thread));
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
}

void test_exiting_waiter_is_not_wakeable_and_join_releases_registration(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    Address entry = process->image.entry_point.virtual_addr;
    ThreadID id = unwrap_result(Scheduler__create_thread(scheduler, &process->thread, entry, entry, 0, 0), 0);
    TEST_ASSERT_NOT_EQUAL(0, id);
    ThreadPtr target = unwrap_optional(ProcessThreads__at(&process->threads, 1), (ThreadPtr){});
    ThreadWaitFixture__park(target, word.virtual_addr, THREAD_WAIT_INDEFINITE);
    process->thread.state = PROC_STATE_RUNNING;
    match_result(Scheduler__exit_thread(scheduler, target, 42),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROC_STATE_DEAD, target->state);
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
    size_t operations_freed = KAllocStub__freed_allocation_count_for_size(sizeof(ParkedOperation));
    UserAddress output = unwrap_result(Process__alloc_user_pages(process, sizeof(ThreadJoinResult)), NULL_USER_ADDRESS);
    match_result(Scheduler__join_thread(scheduler, &process->thread, id, (ThreadJoinResultPtr)output.virtual_addr),
                 success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(operations_freed + 1,
                             KAllocStub__freed_allocation_count_for_size(sizeof(ParkedOperation)));
    TEST_ASSERT_EQUAL(1, ProcessThreads__len(&process->threads));
    TEST_ASSERT_EQUAL(0, unwrap_result(Scheduler__wake_atomic(scheduler, &process->thread, word.virtual_addr, 1), -1));
}

void test_syscall_boundary_rejects_oversized_expected_values_and_missing_callers(void)
{
    ProcessPtr process = ThreadWaitFixture__process();
    UserAddress word = ThreadWaitFixture__word(process);
    match_result(
        ThreadWait__handler(kernel, &Some(ProcessPtr, process), word.virtual_addr, UINT64_MAX, 0, 0, 0,
                            &Some(ThreadPtr, &process->thread)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, code);
        }
    );
    match_result(
        ThreadWake__handler(kernel, &Some(ProcessPtr, process), word.virtual_addr, 1, 0, 0, 0, &None(ThreadPtr)),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(THREAD_WAIT_INVALID, code);
        }
    );
}

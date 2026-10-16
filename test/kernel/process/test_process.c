#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/scheduler/parked.h"
#include "../../../kernel/types/optional.h"
#include "../../../kernel/bin/elf.h"
#include "../../../kernel/bin/embedded_binaries.h"
#include "../../../common/services/service_directory.h"

#include "../fixtures/process.h"
#include "../fixtures/kernel.h"
#include "../stubs/panic.h"
#include "../stubs/mmu.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../stubs/process.h"
#include "../stubs/process_context_layout.h"

static KernelPtr kernel;

static Elf load_hello_world_elf(void)
{
    match_optional(
        EmbeddedElf__load(&bstr("hello_world")),
        some(elf)
        {
            return elf;
        },
        none()
        {
            TEST_FAIL();
            return (Elf){};
        }
    );
}

static UserAllocation allocation_at(ProcessPtr process, uint64_t index)
{
    match_optional(
        UserAllocations__at(&process->user_allocations, index),
        some(allocation)
        {
            return allocation;
        },
        none()
        {
            TEST_FAIL();
            return (UserAllocation){};
        }
    );
}

static UserAddress alloc_user_pages_for_test(ProcessPtr process, size_t size)
{
    match_result(
        Process__alloc_user_pages(process, size),
        success(address)
        {
            return address;
        },
        error(code, message)
        {
            TEST_FAIL();
            return (UserAddress){};
        }
    );
}

void setUp(void)
{
    clear_panic_state();
    reset_mmu_state();
    KAllocStub__reset_virtual_translation();
    KAllocStub__reset_allocation_tracking();
    kernel = init_test_kernel();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_process_store_context_copies_saved_frame(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessContext source = {
        .x = {0x1122},
        .sp = 0x3344,
        .elr = 0x5566,
        .spsr = 0x7788,
        .fpcr = 0x99aa,
        .fpsr = 0xbbcc,
    };

    match_result(
        Process__store_context(process, &source),
        success(code)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(source.x[0], Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(source.sp, Process__required_context(process)->sp);
    TEST_ASSERT_EQUAL_UINT64(source.elr, Process__required_context(process)->elr);
    TEST_ASSERT_EQUAL_UINT64(source.spsr, Process__required_context(process)->spsr);
    TEST_ASSERT_EQUAL_UINT64(source.fpcr, Process__required_context(process)->fpcr);
    TEST_ASSERT_EQUAL_UINT64(source.fpsr, Process__required_context(process)->fpsr);
}

void test_process_store_context_returns_error_when_process_is_not_running(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    ProcessContext source = {.elr = 0x5566};
    uint64_t original_elr = Process__required_context(process)->elr;

    match_result(
        Process__store_context(process, &source),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_INVALID_STATE, code);
            TEST_ASSERT_EQUAL_BSTRING(bstr("Process not running state=PROC_STATE_WAITING pid=16 name=hello_world\n"),
                                      message);
        }
    );

    TEST_ASSERT_EQUAL_UINT64(original_elr, Process__required_context(process)->elr);
}

void test_process_save_context_without_a_trap_frame_does_not_change_context(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    uint64_t original_elr = Process__required_context(process)->elr;

    match_result(
        Process__save_context(process, &None(ProcessContextPtr)),
        success(code)
        {
            TEST_ASSERT_EQUAL(PROCESS_CONTEXT_SAVE_OK, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL_UINT64(original_elr, Process__required_context(process)->elr);
}

void test_process_supports_the_complete_signal_range(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    TEST_ASSERT_TRUE(Process__supports_signal(process, PROCESS_SIGNAL_HANGUP));
    TEST_ASSERT_TRUE(Process__supports_signal(process, PROCESS_SIGNAL_THREAD));
    TEST_ASSERT_FALSE(Process__supports_signal(process, 0));
    TEST_ASSERT_FALSE(Process__supports_signal(process, PROCESS_SIGNAL_COUNT + 1));
}

void test_process_keeps_blocked_signals_pending_until_unblocked(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessSignalMask interrupt = 1ULL << (PROCESS_SIGNAL_INTERRUPT - 1);

    match_result(Process__set_signal_mask(process, PROCESS_SIGNAL_MASK_BLOCK, interrupt),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);

    TEST_ASSERT_EQUAL_UINT64(interrupt, Process__pending_signal_mask(process));
    TEST_ASSERT_TRUE(is_nothing(Process__take_pending_signal(process)));

    match_result(Process__set_signal_mask(process, PROCESS_SIGNAL_MASK_UNBLOCK, interrupt),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INTERRUPT,
                      unwrap_optional(Process__take_pending_signal(process), (ProcessSignal)0));
    TEST_ASSERT_FALSE(Process__has_pending_signal(process));
}

void test_process_signal_handler_entry_and_return_restore_context_and_mask(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessSignalMask user_mask = 1ULL << (PROCESS_SIGNAL_USER_1 - 1);
    ProcessSignalMask handler_mask = 1ULL << (PROCESS_SIGNAL_USER_2 - 1);
    ProcessSignalAction action = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
        .mask = handler_mask,
        .flags = 0,
    };
    ProcessContext interrupted = {
        .x = {0x1122},
        .sp = 0x3344,
        .elr = 0x5566,
        .spsr = 0x7788,
    };

    match_result(Process__set_signal_mask(process, PROCESS_SIGNAL_MASK_SET, user_mask),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);

    match_result(
        Process__deliver_pending_signal(process, &interrupted),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_HANDLER, delivery.kind);
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_INTERRUPT, delivery.signal);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_INTERRUPT, interrupted.x[0]);
    TEST_ASSERT_EQUAL_UINT64(action.restorer, interrupted.x[30]);
    TEST_ASSERT_EQUAL_UINT64(action.handler, interrupted.elr);
    TEST_ASSERT_EQUAL_UINT64(user_mask | handler_mask | (1ULL << (PROCESS_SIGNAL_INTERRUPT - 1)),
                             Process__signal_mask(process));
    TEST_ASSERT_EQUAL_UINT64(1, ProcessSignalFrames__len(&process->signal_frames));

    match_result(Process__restore_signal_context(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(user_mask, Process__signal_mask(process));
    TEST_ASSERT_EQUAL_UINT64(0, ProcessSignalFrames__len(&process->signal_frames));
    TEST_ASSERT_EQUAL_UINT64(0x1122, Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(0x3344, Process__required_context(process)->sp);
    TEST_ASSERT_EQUAL_UINT64(0x5566, Process__required_context(process)->elr);
    TEST_ASSERT_EQUAL_UINT64(0x7788, Process__required_context(process)->spsr);
}

void test_process_signal_handler_interrupts_nonrestartable_wait_with_eintr(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    ProcessSignalAction action = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
    };
    ProcessContext interrupted = {
        .x = {0x1122},
        .elr = 0x5568,
    };
    KernelContext kernel_context = {0};
    ParkedOperationPtr operation = ParkedOperation__heap_new(&kernel_context, &None(Continuation), PARK_SLEEP);
    match_result(Process__push_parked_operation(process, operation),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);

    match_result(
        Process__deliver_pending_signal(process, &interrupted),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_HANDLER, delivery.kind);
            match_optional(
                delivery.interrupted_operation,
                some(interrupted_operation)
                {
                    TEST_ASSERT_EQUAL_PTR(operation, interrupted_operation);
                    ParkedOperation__dispose(interrupted_operation);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_FALSE(Process__is_parked(process));

    match_result(Process__restore_signal_context(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_INT64(SYSCALL_INTERRUPTED, (int64_t)Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(0x5568, Process__required_context(process)->elr);
}

void test_process_signal_handler_restarts_restartable_wait_when_requested(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    ProcessSignalAction action = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
        .flags = PROCESS_SIGNAL_ACTION_RESTART,
    };
    ProcessContext interrupted = {
        .x = {0x1122},
        .elr = 0x5568,
    };
    KernelContext kernel_context = {0};
    ParkedOperationPtr operation =
        ParkedOperation__heap_new(&kernel_context, &None(Continuation), PARK_WAIT_CHILD_EVENT);
    match_result(Process__push_parked_operation(process, operation),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__set_signal_action(process, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);

    match_result(
        Process__deliver_pending_signal(process, &interrupted),
        success(delivery)
        {
            match_optional(
                delivery.interrupted_operation,
                some(interrupted_operation)
                {
                    ParkedOperation__dispose(interrupted_operation);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    match_result(Process__restore_signal_context(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT64(0x1122, Process__required_context(process)->x[0]);
    TEST_ASSERT_EQUAL_UINT64(0x5564, Process__required_context(process)->elr);
}

void test_process_defers_caught_signals_during_exec_load_but_allows_kill(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext kernel_context = {0};
    ParkedOperationPtr operation = ParkedOperation__heap_new(&kernel_context, &None(Continuation), PARK_WAIT_EXEC_LOAD);
    match_result(Process__push_parked_operation(process, operation),
        success_do_nothing(),
        error_test_fail()
    );

    Process__queue_signal(process, PROCESS_SIGNAL_INTERRUPT);
    TEST_ASSERT_FALSE(Process__can_resume_for_signal(process));
    Process__queue_signal(process, PROCESS_SIGNAL_KILL);
    TEST_ASSERT_TRUE(Process__can_resume_for_signal(process));
}

void test_process_alarm_replaces_cancels_and_expires_once(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    TEST_ASSERT_EQUAL_UINT64(0, Process__set_alarm(process, 1000000, 5));
    TEST_ASSERT_EQUAL_UINT64(3, Process__set_alarm(process, 3200000, 7));
    TEST_ASSERT_FALSE(Process__take_expired_alarm(process, 10199999));
    TEST_ASSERT_TRUE(Process__take_expired_alarm(process, 10200000));
    TEST_ASSERT_FALSE(Process__take_expired_alarm(process, 10200000));
    TEST_ASSERT_EQUAL_UINT64(0, Process__set_alarm(process, 11000000, 4));
    TEST_ASSERT_EQUAL_UINT64(4, Process__set_alarm(process, 11000000, 0));
    TEST_ASSERT_FALSE(Process__take_expired_alarm(process, 20000000));
}

void test_process_default_and_ignored_signal_dispositions_are_delivered(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessContext context = {0};

    Process__queue_signal(process, PROCESS_SIGNAL_CHILD);
    match_result(
        Process__deliver_pending_signal(process, &context),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_NONE, delivery.kind);
        },
        error_test_fail()
    );

    Process__queue_signal(process, PROCESS_SIGNAL_TERMINATE);
    match_result(
        Process__deliver_pending_signal(process, &context),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_TERMINATE, delivery.kind);
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_TERMINATE, delivery.signal);
        },
        error_test_fail()
    );
}

void test_process_default_stop_and_continue_dispositions_are_distinct(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessContext context = {0};

    Process__queue_signal(process, PROCESS_SIGNAL_TERMINAL_STOP);
    match_result(
        Process__deliver_pending_signal(process, &context),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_STOP, delivery.kind);
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_TERMINAL_STOP, delivery.signal);
        },
        error_test_fail()
    );

    Process__queue_signal(process, PROCESS_SIGNAL_CONTINUE);
    match_result(
        Process__deliver_pending_signal(process, &context),
        success(delivery)
        {
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_DELIVERY_CONTINUE, delivery.kind);
            TEST_ASSERT_EQUAL(PROCESS_SIGNAL_CONTINUE, delivery.signal);
        },
        error_test_fail()
    );
}

void test_process_stop_and_continue_generation_discards_conflicting_pending_signals(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    Process__queue_signal(process, PROCESS_SIGNAL_TERMINAL_STOP);
    Process__queue_signal(process, PROCESS_SIGNAL_CONTINUE);
    TEST_ASSERT_TRUE(Process__has_pending_resume_signal(process));
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_CONTINUE,
                      unwrap_optional(Process__take_pending_signal(process), (ProcessSignal)0));
    TEST_ASSERT_FALSE(Process__has_pending_signal(process));

    Process__queue_signal(process, PROCESS_SIGNAL_CONTINUE);
    Process__queue_signal(process, PROCESS_SIGNAL_TERMINAL_INPUT);
    TEST_ASSERT_FALSE(Process__has_pending_resume_signal(process));
    TEST_ASSERT_EQUAL(PROCESS_SIGNAL_TERMINAL_INPUT,
                      unwrap_optional(Process__take_pending_signal(process), (ProcessSignal)0));
    TEST_ASSERT_FALSE(Process__has_pending_signal(process));
}

void test_process_child_event_selection_preserves_unrequested_state_changes(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    ProcessChildEvent stopped = {
        .pid = 42,
        .status = PROCESS_WAIT_STATUS_STOPPED(PROCESS_SIGNAL_TERMINAL_STOP),
        .type = PROCESS_CHILD_EVENT_STOPPED,
    };
    ProcessChildEvent exited = {
        .pid = 43,
        .status = PROCESS_WAIT_STATUS_EXITED(7),
        .type = PROCESS_CHILD_EVENT_EXITED,
    };
    match_result(Process__push_child_event(process, &stopped),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__push_child_event(process, &exited),
        success_do_nothing(),
        error_test_fail()
    );

    match_result(
        Process__pop_child_event(process, -1, 0),
        success(event)
        {
            TEST_ASSERT_EQUAL_UINT64(exited.pid, event.pid);
        },
        error_test_fail()
    );
    match_result(
        Process__pop_child_event(process, -1, PROCESS_CHILD_WAIT_UNTRACED),
        success(event)
        {
            TEST_ASSERT_EQUAL_UINT64(stopped.pid, event.pid);
        },
        error_test_fail()
    );
}

void test_setup_from_elf(void)
{
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            system_info.default_user_stack_size = 2 * 1024 * 1024;
            kernel->system_info = Some(SystemInfo, system_info);
        },
        none_test_fail()
    );
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT_EQUAL(Elf__read_header(&elf)->entry, process->entry_point.virtual_addr);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), 0x400000, true, false,
                                             true, PAGE_SIZE));
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), BASE_STACK, true, true,
                                             false, 2 * 1024 * 1024));
            TEST_ASSERT_EQUAL_UINT64(2 * 1024 * 1024, process->stack.size);
        },
        error_test_fail()
    );
}

void test_setup_maps_service_directory(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                             HUND_SERVICE_DIRECTORY_VADDR, true, false, false,
                                             HUND_SERVICE_DIRECTORY_SIZE));
        },
        error_test_fail()
    );
}

void test_setup_tracks_static_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            Optional(UserAllocation) args_allocation = UserAllocations__at(&process->user_allocations, 0);
            Optional(UserAllocation) elf_allocation = UserAllocations__at(&process->user_allocations, 1);
            Optional(UserAllocation) stack_allocation = UserAllocations__at(&process->user_allocations, 2);
            UserAllocation args = allocation_at(process, 0);
            UserAllocation elf = allocation_at(process, 1);
            UserAllocation stack = allocation_at(process, 2);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, args.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, args.size);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_ELF, elf.type);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_STACK, stack.type);
        },
        error_test_fail()
    );
}

void test_fork_eagerly_copies_private_memory_and_resets_process_state(void)
{
    PageTableStub__reset_publish_tracking();
    ProcessPtr parent = dummy_process(80, 0, PROC_STATE_RUNNING);
    Process__set_identity(parent, PROCESS_USER_USER, PROCESS_GROUP_USER, PROCESS_CAP_NONE);
    ProcessSignalMask blocked = 1ULL << (PROCESS_SIGNAL_USER_1 - 1);
    ProcessSignalAction action = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
    };
    parent->pending_signals = 1ULL << (PROCESS_SIGNAL_INTERRUPT - 1);
    match_result(Process__set_signal_mask(parent, PROCESS_SIGNAL_MASK_SET, blocked),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Process__set_signal_action(parent, PROCESS_SIGNAL_INTERRUPT, &action),
        success_do_nothing(),
                 error_test_fail()
    );
    UserAllocation shared = {.type = USER_ALLOCATION_SHARED};
    match_result(UserAllocations__push(&parent->user_allocations, shared),
        success_do_nothing(),
        error_test_fail()
    );
    parent->queue_wait_control = Some(UserAddress, (UserAddress){.valid = true});
    parent->ipc_doorbell = Some(UserAddress, (UserAddress){.valid = true});
    match_result(
        Process__alloc_user_pages(parent, PAGE_SIZE),
        success(allocation)
        {
            Kernel__memset(allocation.kernel_addr.virtual_addr, 0x5a, PAGE_SIZE);
        },
        error_test_fail()
    );

    match_result(
        Process__fork(parent),
        success(child)
        {
            TEST_ASSERT_EQUAL_UINT64(parent->pid, unwrap_optional(child->parent_pid, 0));
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_USER, child->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_USER, child->group_id);
            TEST_ASSERT_EQUAL_UINT64(0, child->pending_signals);
            TEST_ASSERT_EQUAL_UINT64(blocked, Process__signal_mask(child));
            TEST_ASSERT_EQUAL_UINT64(action.handler, Process__signal_action(child, PROCESS_SIGNAL_INTERRUPT).handler);
            TEST_ASSERT_EQUAL(PROC_STATE_NEW, child->state);
            TEST_ASSERT_EQUAL(UserAllocations__len(&parent->user_allocations) - 1,
                              UserAllocations__len(&child->user_allocations));
            TEST_ASSERT_TRUE(is_nothing(child->queue_wait_control));
            TEST_ASSERT_TRUE(is_nothing(child->ipc_doorbell));
            TEST_ASSERT_EQUAL_UINT64(0, Process__required_context(child)->x[0]);
            TEST_ASSERT_EQUAL_UINT64(UserAllocations__len(&child->user_allocations),
                                     PageTableStub__unpublished_map_count());
            TEST_ASSERT_EQUAL_UINT64(1, PageTableStub__publish_count());

            ListIndex parent_dynamic_index = (ListIndex)UserAllocations__len(&parent->user_allocations) - 1;
            ListIndex child_dynamic_index = (ListIndex)UserAllocations__len(&child->user_allocations) - 1;
            UserAllocation parent_allocation = unwrap_optional(
                UserAllocations__at(&parent->user_allocations, parent_dynamic_index), (UserAllocation){});
            UserAllocation child_allocation =
                unwrap_optional(UserAllocations__at(&child->user_allocations, child_dynamic_index), (UserAllocation){});
            TEST_ASSERT_EQUAL_UINT64(parent_allocation.address.virtual_addr, child_allocation.address.virtual_addr);
            TEST_ASSERT_NOT_EQUAL(parent_allocation.address.kernel_addr.virtual_addr,
                                  child_allocation.address.kernel_addr.virtual_addr);
            TEST_ASSERT_EQUAL_MEMORY((void *)parent_allocation.address.kernel_addr.virtual_addr,
                                     (void *)child_allocation.address.kernel_addr.virtual_addr, PAGE_SIZE);
            *(uint8_t *)child_allocation.address.kernel_addr.virtual_addr = 0xa5;
            TEST_ASSERT_EQUAL_HEX8(0x5a, *(uint8_t *)parent_allocation.address.kernel_addr.virtual_addr);

            match_result(Process__dispose_user_allocations(child),
                success_do_nothing(),
                error_test_fail()
            );
            match_result(Process__dispose_page_table(child),
                success_do_nothing(),
                error_test_fail()
            );
            match_result(Process__dispose(child),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_fork_rejects_service_process_without_creating_a_child(void)
{
    ProcessPtr parent = dummy_process(81, 0, PROC_STATE_RUNNING);
    parent->process_class = PROCESS_CLASS_SERVICE;

    match_result(
        Process__fork(parent),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_FORK_UNSUPPORTED_STATE, code);
        }
    );
}

void test_setup_with_args_sets_up_multiple_argv_entries(void)
{
    Elf elf = load_hello_world_elf();
    ProcessArguments arguments = ProcessArguments__new();
    ProcessArguments__push(&arguments, bstring("one"));
    ProcessArguments__push(&arguments, bstring("two"));
    ProcessArguments__push(&arguments, bstring("three"));

    match_result(
        Process__setup_with_args(kernel, &elf, &None(ProcessPtr), &arguments),
        success(process)
        {
            UserAllocation args = allocation_at(process, 0);
            AddressPtr argv = ptr(args.address.kernel_addr.virtual_addr, Address);
            AddressPtr envp = argv + 5;
            Address user_base = args.address.virtual_addr;
            Address kernel_base = args.address.kernel_addr.virtual_addr;

            TEST_ASSERT_EQUAL(0, argv[4]);
            TEST_ASSERT_EQUAL(0, envp[0]);
            TEST_ASSERT_EQUAL_STRING("hello_world", ptr(kernel_base + (argv[0] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
            TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
            ProcessContextPtr context = Process__required_context(process);
            TEST_ASSERT_EQUAL_UINT64(0, context->x[7]);
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_maps_and_tracks_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__alloc_user_pages(process, 123),
                success(address)
                {
                    TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, address.virtual_addr);
                    UserAllocation allocation = allocation_at(process, 3);
                    TEST_ASSERT_EQUAL(4, process->user_allocations.len);
                    TEST_ASSERT_EQUAL(USER_ALLOCATION_DYNAMIC, allocation.type);
                    TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                    TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                     USER_ALLOC_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_returns_memory_full_when_user_allocation_fails(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            KAllocStub__set_user_alloc_failure(true);
            match_result(
                Process__alloc_user_pages(process, 123),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                    TEST_ASSERT_EQUAL(3, UserAllocations__len(&process->user_allocations));
                }
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_maps_source_pages_into_target_process(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    match_result(
                        Process__map_shared_user_pages(target, source, source_address.virtual_addr, PAGE_SIZE * 2),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr, shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr, shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE * 2, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_preserves_offset_inside_source_page(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE);
                    Address source_offset_address = source_address.virtual_addr + 128;

                    match_result(
                        Process__map_shared_user_pages(target, source, source_offset_address, 256),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + 128, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr + 128,
                                              shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr + 128, shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_maps_offset_range_spanning_next_page(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    Address source_offset_address = source_address.virtual_addr + PAGE_SIZE - 16;

                    match_result(
                        Process__map_shared_user_pages(target, source, source_offset_address, 64),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + PAGE_SIZE - 16, shared.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.virtual_addr + PAGE_SIZE - 16,
                                              shared.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr + PAGE_SIZE - 16,
                                              shared.kernel_addr.phys_addr);

                            UserAllocation allocation = allocation_at(target, 3);
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_SHARED, allocation.type);
                            TEST_ASSERT_EQUAL(PAGE_SIZE * 2, allocation.size);
                            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                             USER_ALLOC_BASE + PAGE_SIZE, true, true, false,
                                                             PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_rejects_unallocated_source_range(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    match_result(
                        Process__map_shared_user_pages(target, source, 0x900000000, PAGE_SIZE),
                        success_test_fail(),
                        error(code, message)
                        {
                            TEST_ASSERT_EQUAL(PROCESS_MEMORY_NOT_FOUND, code);
                        }
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_unmaps_shared_allocation_without_freeing_source(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE);

                    match_result(
                        Process__map_shared_user_pages(target, source, source_address.virtual_addr, PAGE_SIZE),
                        success(shared)
                        {
                            match_result(
                                Process__free_user_pages(target, shared.virtual_addr),
                                success(status)
                                {
                                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, status);
                                },
                                error_test_fail()
                            );

                            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                                   shared.virtual_addr, true, true, false, PAGE_SIZE));
                            match_result(
                                Process__device_address(source, source_address.virtual_addr),
                                success(physical)
                                {
                                    TEST_ASSERT_EQUAL(source_address.kernel_addr.phys_addr, physical);
                                },
                                error_test_fail()
                            );
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_shared_user_pages_with_allocation_returns_mapping_and_allocation_base(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(source)
        {
            match_result(
                Process__setup(kernel, &elf, &None(ProcessPtr)),
                success(target)
                {
                    UserAddress source_address = alloc_user_pages_for_test(source, PAGE_SIZE * 2);
                    Address source_offset_address = source_address.virtual_addr + 123;

                    match_result(
                        Process__map_shared_user_pages_with_allocation(target, source, source_offset_address,
                                                                       PAGE_SIZE),
                        success(shared)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, shared.allocation.virtual_addr);
                            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE + 123, shared.mapping.virtual_addr);

                            match_result(
                                Process__free_user_pages(target, shared.allocation.virtual_addr),
                                success(status)
                                {
                                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, status);
                                },
                                error_test_fail()
                            );

                            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&target->page_table),
                                                                   shared.allocation.virtual_addr, true, true, false,
                                                                   PAGE_SIZE * 2));
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_device_address_resolves_each_kernel_virtual_page_to_physical(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__alloc_user_pages(process, PAGE_SIZE * 2),
                success(address)
                {
                    Address second_kernel_page = address.kernel_addr.virtual_addr + PAGE_SIZE;
                    Address second_physical_page = 0x12345000;
                    KAllocStub__set_virtual_translation(second_kernel_page, PAGE_SIZE, second_physical_page);

                    match_result(
                        Process__device_address(process, address.virtual_addr + PAGE_SIZE + 0x80),
                        success(physical)
                        {
                            TEST_ASSERT_EQUAL(second_physical_page + 0x80, physical);
                        },
                        error_test_fail()
                    );
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_unmaps_and_removes_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            UserAddress address = alloc_user_pages_for_test(process, PAGE_SIZE);

            match_result(
                Process__free_user_pages(process, address.virtual_addr),
                success(code)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(3, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                   USER_ALLOC_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_reuses_freed_dynamic_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            UserAddress first = alloc_user_pages_for_test(process, PAGE_SIZE);
            UserAddress second = alloc_user_pages_for_test(process, PAGE_SIZE);
            TEST_ASSERT_EQUAL(USER_ALLOC_BASE + PAGE_SIZE, first.virtual_addr);
            TEST_ASSERT_EQUAL(first.virtual_addr + PAGE_SIZE, second.virtual_addr);
            TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, unwrap_result(Process__free_user_pages(process, first.virtual_addr),
                                                               PROCESS_MEMORY_FREE_FAILED));

            UserAddress replacement = alloc_user_pages_for_test(process, PAGE_SIZE);
            TEST_ASSERT_EQUAL(first.virtual_addr, replacement.virtual_addr);
        },
        error_test_fail()
    );
}

void test_process_alloc_user_pages_reuses_fragmented_range_only_when_it_fits(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            UserAddress first = alloc_user_pages_for_test(process, PAGE_SIZE);
            UserAddress hole = alloc_user_pages_for_test(process, PAGE_SIZE * 2);
            UserAddress last = alloc_user_pages_for_test(process, PAGE_SIZE);
            TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, unwrap_result(Process__free_user_pages(process, hole.virtual_addr),
                                                               PROCESS_MEMORY_FREE_FAILED));

            UserAddress reused = alloc_user_pages_for_test(process, PAGE_SIZE * 2);
            TEST_ASSERT_EQUAL(hole.virtual_addr, reused.virtual_addr);
            TEST_ASSERT_EQUAL(first.virtual_addr + PAGE_SIZE * 3, last.virtual_addr);
        },
        error_test_fail()
    );
}

void test_process_free_user_pages_rejects_static_allocation(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__free_user_pages(process, BASE_STACK),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_STATIC_ALLOCATION, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_dispose_user_allocations_unmaps_all_user_allocations(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__alloc_user_pages(process, PAGE_SIZE);

            match_result(Process__dispose_user_allocations(process),
                success_do_nothing(),
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(0, process->user_allocations.len);
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), 0x400000, true,
                                                   false, true, 101));
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), BASE_STACK, true,
                                                   true, false, STACK_SIZE));
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), USER_ALLOC_BASE,
                                                   true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_dispose_page_table_frees_owned_pages_and_tracking_nodes(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            size_t page_count = ProcessPageTable__page_count(&process->page_table);
            size_t free_count_before = KAllocStub__free_count();
            size_t mappings_before = MemoryMappings__len(&kernel->memory_accounting.mappings);
            Address space = ProcessPageTable__root(&process->page_table).virtual_addr;

            match_result(Process__dispose_page_table(process),
                success_do_nothing(),
                error_test_fail()
            );

            size_t removed_mappings = mappings_before - MemoryMappings__len(&kernel->memory_accounting.mappings);
            TEST_ASSERT_EQUAL(free_count_before + (page_count * 2) + removed_mappings, KAllocStub__free_count());
            match_optional(
                MemoryAccounting__usage(&kernel->memory_accounting, space),
                some(usage)
                {
                    TEST_ASSERT_EQUAL_UINT64(0, usage.private_bytes);
                    TEST_ASSERT_EQUAL_UINT64(0, usage.shared_bytes);
                    TEST_ASSERT_EQUAL_UINT64(0, usage.device_bytes);
                },
                none_test_fail()
            );
            TEST_ASSERT_EQUAL(0, ProcessPageTable__page_count(&process->page_table));
            TEST_ASSERT_FALSE(ProcessPageTable__root(&process->page_table).valid);
        },
        error_test_fail()
    );
}

void test_process_claimed_device_is_reset_before_memory_disposal(void)
{
    extern Address test_reset_device;
    test_reset_device = 0;
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x0a000000, PAGE_SIZE),
        success(mmio)
        {
            match_result(Process__claim_virtio_device(process, mmio.virtual_addr),
                success_do_nothing(),
                         error_test_fail()
            );
            match_result(Process__dispose_user_allocations(process),
                success_do_nothing(),
                error_test_fail()
            );
            TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr, test_reset_device);
            TEST_ASSERT_EQUAL_UINT64(0, ProcessVirtioDevices__len(&process->virtio_devices));
            test_reset_device = 0;
            Process__quiesce_device(process);
            TEST_ASSERT_EQUAL_UINT64(0, test_reset_device);
        },
        error_test_fail()
    );
}

void test_process_claims_and_quiesces_multiple_virtio_devices(void)
{
    extern Address test_reset_device;
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x0a000000, PAGE_SIZE),
        success(mmio)
        {
            match_result(Process__claim_virtio_device(process, mmio.virtual_addr),
                success_do_nothing(),
                         error_test_fail()
            );
            match_result(Process__claim_virtio_device(process, mmio.virtual_addr + 0x200),
                success_do_nothing(),
                         error_test_fail()
            );
            TEST_ASSERT_EQUAL_UINT64(2, ProcessVirtioDevices__len(&process->virtio_devices));

            test_reset_device = 0;
            Process__quiesce_device(process);
            TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr + 0x200, test_reset_device);
            TEST_ASSERT_EQUAL_UINT64(0, ProcessVirtioDevices__len(&process->virtio_devices));
        },
        error_test_fail()
    );
}

void test_process_dispose_user_allocations_unmaps_mmio_without_freeing_heap(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Address physical = 0x0a000000;
            match_result(
                Process__map_mmio(process, physical, PAGE_SIZE),
                success(mmio)
                {
                    match_optional(
                        UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1),
                        some(allocation)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(mmio.virtual_addr, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(physical, allocation.address.kernel_addr.virtual_addr);
                        },
                        none_test_fail()
                    );

                    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                          mmio.virtual_addr, true, true, false, PAGE_SIZE));

                    match_result(
                        Process__dispose_user_allocations(process),
                        success(code)
                        {
                            TEST_ASSERT_EQUAL(PROCESS_MEMORY_OK, code);
                        },
                        error_test_fail()
                    );

                    TEST_ASSERT_EQUAL(0, process->user_allocations.len);
                    TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                           mmio.virtual_addr, true, true, false, PAGE_SIZE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_mmio_returns_pointer_at_requested_physical_offset(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Address physical = 0x0a000200;
            match_result(
                Process__map_mmio(process, physical, 0x200),
                success(mmio)
                {
                    Address expected_base = USER_ALLOC_BASE + PAGE_SIZE;
                    TEST_ASSERT_EQUAL(expected_base + 0x200, mmio.virtual_addr);
                    TEST_ASSERT_EQUAL(physical, mmio.kernel_addr.virtual_addr);

                    match_optional(
                        UserAllocations__at(&process->user_allocations, process->user_allocations.len - 1),
                        some(allocation)
                        {
                            TEST_ASSERT_EQUAL(USER_ALLOCATION_MMIO, allocation.type);
                            TEST_ASSERT_EQUAL(expected_base, allocation.address.virtual_addr);
                            TEST_ASSERT_EQUAL(0x0a000000, allocation.address.kernel_addr.virtual_addr);
                            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
                        },
                        none_test_fail()
                    );

                    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                          mmio.virtual_addr, true, true, false, 0x200));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_map_trapped_mmio_records_authorized_range_without_page_mapping(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_trapped_mmio(process, 0x70000000, PAGE_SIZE),
        success(mmio)
        {
            TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                                   mmio.virtual_addr, true, true, false, PAGE_SIZE));
            match_optional(
                Process__readable_mmio_mapping(process, mmio.virtual_addr + 0x84, sizeof(uint32_t)),
                some(mapping)
                {
                    TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr + 0x84, mapping.virtual_addr);
                    TEST_ASSERT_EQUAL_UINT64(0x70000084, mapping.kernel_addr.phys_addr);
                },
                none_test_fail()
            );
            match_optional(
                Process__writable_mmio_mapping(process, mmio.virtual_addr + 0x84, sizeof(uint32_t)),
                some(mapping)
                {
                    TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr + 0x84, mapping.virtual_addr);
                    TEST_ASSERT_EQUAL_UINT64(0x70000084, mapping.kernel_addr.phys_addr);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_writable_mmio_mapping_resolves_contained_range(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x70000000, PAGE_SIZE),
        success(mmio)
        {
            match_optional(
                Process__writable_mmio_mapping(process, mmio.virtual_addr + 0x84, sizeof(uint32_t)),
                some(mapping)
                {
                    TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr + 0x84, mapping.virtual_addr);
                    TEST_ASSERT_EQUAL_UINT64(0x70000084, mapping.kernel_addr.phys_addr);
                    TEST_ASSERT_TRUE(mapping.write);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_readable_mmio_mapping_resolves_contained_range(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x70000000, PAGE_SIZE),
        success(mmio)
        {
            match_optional(
                Process__readable_mmio_mapping(process, mmio.virtual_addr + 0x84, sizeof(uint32_t)),
                some(mapping)
                {
                    TEST_ASSERT_EQUAL_UINT64(mmio.virtual_addr + 0x84, mapping.virtual_addr);
                    TEST_ASSERT_EQUAL_UINT64(0x70000084, mapping.kernel_addr.phys_addr);
                    TEST_ASSERT_TRUE(mapping.read);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_readable_mmio_mapping_rejects_write_only_mmio(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    UserAddress address = UserAddress__new(0x50000000, KERNEL_ADDR(0x70000000, 0x70000000), false, true, false, true);
    match_result(
        Process__record_user_allocation(process, address, PAGE_SIZE, USER_ALLOCATION_MMIO),
        success(_)
        {
            TEST_ASSERT_TRUE(
                is_nothing(Process__readable_mmio_mapping(process, address.virtual_addr, sizeof(uint32_t))));
        },
        error_test_fail()
    );
}

void test_process_writable_mmio_mapping_rejects_range_past_allocation(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    match_result(
        Process__map_mmio(process, 0x70000000, PAGE_SIZE),
        success(mmio)
        {
            TEST_ASSERT_TRUE(is_nothing(
                Process__writable_mmio_mapping(process, mmio.virtual_addr + PAGE_SIZE - 1, sizeof(uint32_t))));
        },
        error_test_fail()
    );
}

void test_process_writable_mmio_mapping_rejects_read_only_mmio(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    UserAddress address = UserAddress__new(0x40000000, KERNEL_ADDR(0x70000000, 0x70000000), true, false, false, true);
    match_result(
        Process__record_user_allocation(process, address, PAGE_SIZE, USER_ALLOCATION_MMIO),
        success(_)
        {
            TEST_ASSERT_TRUE(
                is_nothing(Process__writable_mmio_mapping(process, address.virtual_addr, sizeof(uint32_t))));
        },
        error_test_fail()
    );
}

void test_process_writable_user_kernel_mapping_resolves_writable_range(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    UserAddress address = alloc_user_pages_for_test(process, PAGE_SIZE);

    match_optional(
        Process__writable_user_kernel_mapping(process, address.virtual_addr + 64, sizeof(SpawnAbortReport)),
        some(mapping)
        {
            TEST_ASSERT_EQUAL_HEX64(address.kernel_addr.virtual_addr + 64, mapping.virtual_addr);
            TEST_ASSERT_EQUAL_HEX64(address.kernel_addr.phys_addr + 64, mapping.phys_addr);
        },
        none_test_fail()
    );
}

void test_process_writable_user_kernel_mapping_rejects_read_only_and_uncontained_ranges(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    UserAddress address = UserAddress__new(0x30000000, KERNEL_ADDR(0x70000000, 0x80000000), true, false, false, true);
    match_result(
        Process__record_user_allocation(process, address, PAGE_SIZE, USER_ALLOCATION_STACK),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_TRUE(is_nothing(
        Process__writable_user_kernel_mapping(process, address.virtual_addr, sizeof(SpawnAbortReport))));
    UserAddress writable =
        UserAddress__new(0x30002000, KERNEL_ADDR(0x70002000, 0x80002000), true, true, false, true);
    match_result(
        Process__record_user_allocation(process, writable, PAGE_SIZE, USER_ALLOCATION_STACK),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Process__writable_user_kernel_mapping(
        process, writable.virtual_addr + PAGE_SIZE - 1, sizeof(SpawnAbortReport))));

    UserAddress missing_kernel_alias = UserAddress__new(0x30004000, NULL_KERNEL_ADDRESS, true, true, false, true);
    match_result(
        Process__record_user_allocation(process, missing_kernel_alias, PAGE_SIZE, USER_ALLOCATION_STACK),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(Process__writable_user_kernel_mapping(
        process, missing_kernel_alias.virtual_addr, sizeof(SpawnAbortReport))));
}

void test_process_sbrk_maps_pages_at_heap_break(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__sbrk(process, 123),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 3);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table), USER_HEAP_BASE, true,
                                             true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_sbrk_growth_inside_mapped_page_does_not_map_more_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, 123);

            match_result(
                Process__sbrk(process, 100),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE + 123, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + 223, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(4, process->user_allocations.len);
        },
        error_test_fail()
    );
}

void test_process_sbrk_growth_across_page_maps_from_previous_mapped_end(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, 123);
            Process__sbrk(process, PAGE_SIZE);

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE * 2, process->heap_mapped_end);
            TEST_ASSERT_EQUAL(5, process->user_allocations.len);

            UserAllocation allocation = allocation_at(process, 4);
            TEST_ASSERT_EQUAL(USER_ALLOCATION_HEAP, allocation.type);
            TEST_ASSERT_EQUAL(PAGE_SIZE, allocation.size);
            TEST_ASSERT(check_address_mapped(ProcessPageTable__root_table(&process->page_table),
                                             USER_HEAP_BASE + PAGE_SIZE, true, true, false, PAGE_SIZE));
        },
        error_test_fail()
    );
}

void test_process_sbrk_can_shrink_without_unmapping_pages(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            Process__sbrk(process, PAGE_SIZE);

            match_result(
                Process__sbrk(process, -100),
                success(old_break)
                {
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, old_break);
                },
                error_test_fail()
            );

            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE - 100, process->heap_break);
            TEST_ASSERT_EQUAL(USER_HEAP_BASE + PAGE_SIZE, process->heap_mapped_end);
        },
        error_test_fail()
    );
}

void test_process_sbrk_rejects_shrink_below_heap_base(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_result(
                Process__sbrk(process, -1),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_allocations_cannot_grow_into_service_or_shared_page_regions(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            match_optional(
                UserAllocations__ptr_at(&process->user_allocations, 0),
                some(allocation)
                {
                    allocation->address.virtual_addr = USER_ALLOC_BASE;
                    allocation->size = HUND_SERVICE_DIRECTORY_VADDR - USER_ALLOC_BASE;
                },
                none_test_fail()
            );
            match_result(
                Process__alloc_user_pages(process, PAGE_SIZE),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                }
            );
            match_result(
                Process__map_mmio(process, 0x1000, PAGE_SIZE),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                }
            );
            match_result(
                Process__sbrk(process, USER_ALLOC_BASE - USER_HEAP_BASE + 1),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_INVALID_SIZE, code);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_sbrk_returns_memory_full_when_user_allocation_fails(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            KAllocStub__set_user_alloc_failure(true);
            match_result(
                Process__sbrk(process, 123),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(PROCESS_MEMORY_FULL, code);
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, process->heap_break);
                    TEST_ASSERT_EQUAL(USER_HEAP_BASE, process->heap_mapped_end);
                }
            );
        },
        error_test_fail()
    );
}

void test_process_setup_defaults_to_root_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(process)
        {
            TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_ROOT, process->user_id);
            TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_ROOT, process->group_id);
            TEST_ASSERT_TRUE(Process__is_root(process));
            TEST_ASSERT_TRUE(Process__has_capability(process, PROCESS_CAP_MAP_MMIO));
        },
        error_test_fail()
    );
}

void test_process_child_inherits_parent_identity(void)
{
    Elf elf = load_hello_world_elf();
    match_result(
        Process__setup(kernel, &elf, &None(ProcessPtr)),
        success(parent)
        {
            Process__set_hund_identity(parent);

            match_result(
                Process__setup(kernel, &elf, &Some(ProcessPtr, parent)),
                success(child)
                {
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_HUND, child->user_id);
                    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_HUND, child->group_id);
                    TEST_ASSERT_FALSE(Process__is_root(child));
                    TEST_ASSERT_TRUE(Process__has_capability(child, PROCESS_CAP_REGISTER_BLOCK_DEVICE));
                },
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_process_mark_parked_operation_ready_matches_operation_key(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext first_context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    KernelContext second_context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    ParkedOperationPtr first = ParkedOperation__heap_new(&first_context, &None(Continuation), PARK_WAIT_IO);
    ParkedOperationPtr second = ParkedOperation__heap_new(&second_context, &None(Continuation), PARK_WAIT_IO);
    first->operation_key = Some(OperationKey, 10);
    second->operation_key = Some(OperationKey, 20);

    match_result(Process__push_parked_operation(process, first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(Process__push_parked_operation(process, second),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_TRUE(
        Process__mark_parked_operation_ready(process, PARK_WAIT_IO, &Some(OperationKey, 20), &Some(ReturnCode, 123)));
    TEST_ASSERT_FALSE(Process__has_ready_parked_operation(process, PARK_WAIT_IO, &Some(OperationKey, 10)));
    TEST_ASSERT_TRUE(Process__has_ready_parked_operation(process, PARK_WAIT_IO, &Some(OperationKey, 20)));

    match_optional(
        Process__pop_ready_parked_operation(process),
        some(ready)
        {
            TEST_ASSERT_EQUAL_PTR(second, ready);
            ParkedOperation__dispose(ready);
        },
        none_test_fail()
    );

    match_optional(
        Process__pop_ready_parked_operation(process),
        some(ready)
        {
            ParkedOperation__dispose(ready);
            TEST_FAIL();
        },
        none_do_nothing()
    );

    match_optional(
        Process__pop_parked_operation(process),
        some(remaining)
        {
            TEST_ASSERT_EQUAL_PTR(first, remaining);
            ParkedOperation__dispose(remaining);
        },
        none_test_fail()
    );
}

void test_process_wake_expired_sleep_ignores_process_that_is_not_sleeping(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_WAITING);
    KernelContext context = KernelContext__new(&None(ProcessID), &None(SchedulerPtr));
    ParkedOperationPtr sleep = ParkedOperation__heap_new(&context, &None(Continuation), PARK_SLEEP);
    sleep->operation_key = Some(OperationKey, 1);

    match_result(Process__push_parked_operation(process, sleep),
        success_do_nothing(),
        error_test_fail()
    );

    TEST_ASSERT_FALSE(Process__wake_expired_sleep(process, 2));
    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, process->state);
    TEST_ASSERT_FALSE(ParkedOperation__is_ready(sleep));

    match_optional(
        Process__pop_parked_operation(process),
        some(remaining)
        {
            TEST_ASSERT_EQUAL_PTR(sleep, remaining);
            ParkedOperation__dispose(remaining);
        },
        none_test_fail()
    );
}

void test_terminal_endpoints_inherit_without_kernel_io_resources(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    parent->startup_stdio =
        SpawnStdio__new(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    ProcessPtr child = dummy_process_with_parent(2, 0, PROC_STATE_READY, parent);
    Process__prepare_startup_stdio(child);
    TestStartupStdio stdio = startup_stdio();
    TEST_ASSERT_EQUAL_UINT64(child->pid, stdio.pid);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_TTY_INPUT, stdio.input);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, stdio.output);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, stdio.error);
}

void test_spawn_stdio_resolves_standard_endpoints_and_rejects_general_handles(void)
{
    ProcessPtr parent = dummy_process(1, 0, PROC_STATE_RUNNING);
    parent->startup_stdio =
        SpawnStdio__new(IO_ENDPOINT_TTY_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    SpawnStdio request = SpawnStdio__new(IO_ENDPOINT_KEYBOARD_INPUT, 2, 1);
    match_result(
        Process__stdio_for_spawn(parent, &request),
        success(stdio)
        {
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_KEYBOARD_INPUT, SpawnStdio__stdin_fd(stdio));
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stdout_fd(stdio));
            TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stderr_fd(stdio));
        },
        error_test_fail()
    );
    request = SpawnStdio__new(7, 1, 2);
    match_result(
        Process__stdio_for_spawn(parent, &request),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
}

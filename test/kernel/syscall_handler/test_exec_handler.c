#include "unity.h"

#include "../../../common_includes/syscalls/spawn.h"
#include "../../../kernel/syscall/auth.h"
#include "../../../kernel/syscall/exec_handler.h"
#include "../../../user/lib/newlib/exec_payload.c"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/process.h"
#include "../stubs/process_context_layout.h"
#include "../fixtures/exec_channel.h"

static FakeExecService filesystem;
FixedArrayDefine(BString, 4, BootEndpointNames, bstring_equals);
FixedArrayBodyDefine(BootEndpointNames);

#define Exec__handler(kernel, proc, request, ...) FakeExecService__spawn(&filesystem, (kernel), &(proc), request)
#define ExecRequest__handler(kernel, proc, request, ...) FakeExecService__spawn(&filesystem, (kernel), &(proc), request)

static KernelPtr kernel = NULL;
static ProcessPtr parent = NULL;
static BString exec_path = bstr("/sbin/hello_world");
static const CPUID cpu = 0;

static SpawnRequest test_spawn_request(const char * raw_path, SpawnMode mode, AuthTicketID auth_ticket_id,
                                       SpawnStdio stdio)
{
    BString path = bstring(raw_path);
    SpawnArguments arguments = SpawnArguments__new();
    return SpawnRequest__new(mode, &path, SPAWN_INHERIT_USER_ID, auth_ticket_id, stdio, &arguments);
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
        }
    );

    return (UserAllocation){0};
}

static Elf dynamic_exec_image(uint8_t * image, size_t size)
{
    Elf source = unwrap_optional(EmbeddedElf__load(&bstr("hello_world")), (Elf){});
    Elf64HeaderPtr header = ptr(image, Elf64Header);
    ProgramHeaderPtr program_headers = ptr(image + sizeof(Elf64Header), ProgramHeader);
    size_t interpreter_offset = sizeof(Elf64Header) + 2 * sizeof(ProgramHeader);

    *header = (Elf64Header){
        .magic = {0x7f, 'E', 'L', 'F'},
        .klass = 2,
        .endianness = LittleEndian,
        .ei_version = 1,
        .type = Shared,
        .machine = Elf__read_header(&source)->machine,
        .elf_version = 1,
        .header_size = sizeof(Elf64Header),
        .program_header_offset = sizeof(Elf64Header),
        .program_header_entry_size = sizeof(ProgramHeader),
        .program_header_entries = 2,
    };
    program_headers[0] = (ProgramHeader){
        .type = Interpreter,
        .segment_offset = interpreter_offset,
        .file_size = sizeof(WUFF_INTERPRETER_PATH),
        .memory_size = sizeof(WUFF_INTERPRETER_PATH),
    };
    program_headers[1] = (ProgramHeader){.type = Dynamic};
    Kernel__memcopy((Address)WUFF_INTERPRETER_PATH, (Address)(image + interpreter_offset),
                    sizeof(WUFF_INTERPRETER_PATH));
    return Elf__new((Address)image, (Address)(image + size), &bstr("/dynamic"));
}

static void use_dynamic_exec_image(uint8_t * image, size_t size)
{
    Elf elf = dynamic_exec_image(image, size);
    filesystem.override_path = elf.name;
    filesystem.override_image = elf.start;
    filesystem.override_image_size = elf.end - elf.start;
}

void setUp(void)
{
    clear_deferred_syscall_result();
    current_process_reset();
    kernel = init_test_kernel();
    parent = dummy_process(1, cpu, PROC_STATE_RUNNING);
    Process__set_hund_identity(parent);
    Scheduler__schedule(kernel->scheduler, parent);
    Scheduler__preempt(kernel->scheduler, cpu, &None(ProcessContextPtr));
    filesystem = FakeExecService__new(kernel);
}

void tearDown(void)
{
}

void test_boot_handoff_requires_mama_and_registered_services_and_cannot_be_reversed(void)
{
    Optional(ProcessPtr) caller = Some(ProcessPtr, parent);
    SpawnRequest request =
        test_spawn_request("/sbin/hello_world", P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);
    match_result(BootSpawn__handler(kernel, &caller, (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
                 error_test_ok()
    );
    kernel->boot_authority = parent->pid;
    match_result(BootComplete__handler(kernel, &caller, 0, 0, 0, 0, 0),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_FALSE(kernel->boot_complete);
    match_result(BootSpawn__handler(kernel, &caller, (Anything)&request, 0, 0, 0, 0),
        success_do_nothing(),
                 error_test_fail()
    );
    ServiceDirectoryPtr directory = KernelServiceDirectory__kernel_directory(&kernel->service_directory);
    QueueEndpointID endpoint = unwrap_optional(ServiceDirectory__find_endpoint(directory, &bstr(IO_SYSTEM_SERVICE)), 0);
    BootEndpointNames names = BootEndpointNames__new();
    BootEndpointNames__push(&names, bstr(IO_FILESYSTEM_SERVICE));
    BootEndpointNames__push(&names, bstr("/sbin"));
    BootEndpointNames__push(&names, bstr("/boot"));
    BootEndpointNames__push(&names, bstr("/boot/config"));
    for_each_fa(
        BootEndpointNames,
        names,
        index,
        name,
        {
            match_result(KernelServiceDirectory__publish_endpoint(&kernel->service_directory, filesystem.io,
                                                                  &name, endpoint),
                         success_do_nothing(),
                error_test_fail()
            );
        }
    );
    match_result(BootComplete__handler(kernel, &caller, 0, 0, 0, 0, 0),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_FALSE(kernel->boot_complete);
    match_result(KernelServiceDirectory__publish_endpoint(&kernel->service_directory, filesystem.puffer,
                                                          &bstr(BUFFER_SERVICE_NAME), endpoint),
                 success_do_nothing(),
        error_test_fail()
    );
    match_result(BootComplete__handler(kernel, &caller, 0, 0, 0, 0, 0),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(kernel->boot_complete);
    match_result(BootComplete__handler(kernel, &caller, 0, 0, 0, 0, 0),
        success_test_fail(),
        error_test_ok()
    );
    match_result(BootSpawn__handler(kernel, &caller, (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
                 error_test_ok()
    );
    match_result(FakeExecService__spawn(&filesystem, kernel, &caller, (Anything)&request),
        success_do_nothing(),
                 error_test_fail()
    );
    TEST_ASSERT_GREATER_THAN(1, filesystem.reads);
}

void test_normal_spawn_does_not_fall_back_to_embedded_files_when_service_is_absent(void)
{
    QueueEndpointID endpoint = unwrap_optional(
        ServiceDirectory__find_endpoint(KernelServiceDirectory__kernel_directory(&kernel->service_directory),
                                        &bstr(IO_SYSTEM_SERVICE)),
        0);
    KernelServiceDirectory__remove_endpoint(&kernel->service_directory, endpoint);
    SpawnRequest request =
        test_spawn_request("/sbin/hello_world", P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);
    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_NO_FILE, code);
        }
    );
    TEST_ASSERT_TRUE(is_nothing(parent->exec_load));
}

void test_process_cleanup_cancels_pending_executable_load(void)
{
    SpawnRequest request =
        test_spawn_request("/sbin/hello_world", P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);
    Optional(ProcessPtr) caller = Some(ProcessPtr, parent);
    (ExecRequest__handler)(kernel, &caller, (Anything)&request, 0, 0, 0, 0);
    FakeExecService__poll(&filesystem, kernel, parent);
    match_result(Process__dispose_owned_kernel_resources(parent),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(is_nothing(parent->exec_load));
    match_optional(
        filesystem.control,
        some(control)
        {
            TEST_ASSERT_EQUAL(QUEUE_CLOSED, Queue__status(&control));
        },
        none_test_fail()
    );
    FakeExecService__drain(&filesystem, kernel);
    TEST_ASSERT_FALSE(Queues__has_kernel_client(&kernel->scheduler->queues, filesystem.io));
}

void test_exec_is_called_triggering_new_process()
{
    SpawnRequest request = test_spawn_request(cstr(exec_path), P_WAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);

    match_result(
        Exec__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_GREATER_THAN(0, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_WAITING, parent->state);
}

void test_exec_handler_nowait_spawns_without_parking_parent()
{
    SpawnRequest request =
        test_spawn_request(cstr(exec_path), P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);

    match_result(
        Exec__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_GREATER_THAN(0, code);
        },
        error_test_fail()
    );

    TEST_ASSERT_EQUAL(PROC_STATE_RUNNING, parent->state);
}

void test_exec_redirect_preserves_requested_process_name(void)
{
    filesystem.redirect_open = true;
    SpawnRequest request =
        test_spawn_request(cstr(exec_path), P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT);

    match_result(
        Exec__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(pid)
        {
            match_optional(
                Scheduler__find_process_by_pid(kernel->scheduler, pid),
                some(child)
                {
                    TEST_ASSERT_TRUE(filesystem.redirected);
                    TEST_ASSERT_EQUAL_BSTRING(exec_path, child->name);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_exec_request_handler_passes_spawn_arguments_to_process_argv(void)
{
    SpawnArguments arguments = SpawnArguments__new();
    match_result(SpawnArguments__push(&arguments, bstring("one")),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(SpawnArguments__push(&arguments, bstring("two")),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(SpawnArguments__push(&arguments, bstring("three")),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(SpawnArguments__push(&arguments, bstring("four")),
        success_do_nothing(),
        error_test_fail()
    );
    BString path = bstring("/sbin/hello_world");
    SpawnRequest request = SpawnRequest__new(P_NOWAIT, &path, SPAWN_INHERIT_USER_ID, SPAWN_INHERIT_AUTH_TICKET,
                                             SPAWN_STDIO_DEFAULT, &arguments);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(pid)
        {
            match_optional(
                Scheduler__find_process_by_pid(kernel->scheduler, pid),
                some(child)
                {
                    UserAllocation args = allocation_at(child, 0);
                    AddressPtr argv = ptr(args.address.kernel_addr.virtual_addr, Address);
                    Address user_base = args.address.virtual_addr;
                    Address kernel_base = args.address.kernel_addr.virtual_addr;

                    TEST_ASSERT_EQUAL(0, argv[5]);
                    TEST_ASSERT_EQUAL_STRING("/sbin/hello_world", ptr(kernel_base + (argv[0] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("one", ptr(kernel_base + (argv[1] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("two", ptr(kernel_base + (argv[2] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("three", ptr(kernel_base + (argv[3] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("four", ptr(kernel_base + (argv[4] - user_base), char));
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_dynamic_spawn_loads_static_interpreter_and_preserves_requested_startup(void)
{
    uint8_t image[256] = {0};
    use_dynamic_exec_image(image, sizeof(image));
    SpawnArguments arguments = SpawnArguments__new();
    match_result(SpawnArguments__push(&arguments, bstr("argument")),
        success_do_nothing(),
        error_test_fail()
    );
    SpawnRequest request = SpawnRequest__new(P_NOWAIT, &bstr("/dynamic"), SPAWN_INHERIT_USER_ID,
                                             SPAWN_INHERIT_AUTH_TICKET, SPAWN_STDIO_DEFAULT, &arguments);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(pid)
        {
            match_optional(
                Scheduler__find_process_by_pid(kernel->scheduler, pid),
                some(child)
                {
                    TEST_ASSERT_EQUAL_BSTRING(bstr("/dynamic"), child->name);
                    UserAllocation startup = allocation_at(child, 0);
                    AddressPtr argv = ptr(startup.address.kernel_addr.virtual_addr, Address);
                    Address user_base = startup.address.virtual_addr;
                    Address kernel_base = startup.address.kernel_addr.virtual_addr;
                    TEST_ASSERT_EQUAL_STRING("/dynamic", ptr(kernel_base + (argv[0] - user_base), char));
                    TEST_ASSERT_EQUAL_STRING("argument", ptr(kernel_base + (argv[1] - user_base), char));
                    TEST_ASSERT_EQUAL_UINT64(0, Process__required_context(child)->x[7]);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_UINT(2, filesystem.opens);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dynamic"), filesystem.first_path);
    TEST_ASSERT_EQUAL_BSTRING(bstr(WUFF_INTERPRETER_PATH), filesystem.second_path);
}

void test_dynamic_overlay_loads_static_interpreter_and_preserves_exec_payload(void)
{
    uint8_t image[256] = {0};
    use_dynamic_exec_image(image, sizeof(image));
    ProcessID pid = parent->pid;
    char * arguments[] = {"dynamic", NULL};
    char * environment[] = {"PATH=/usr/bin", NULL};
    ExecPayloadPtr payload = PosixExecPayload__new("/dynamic", "/work", arguments, environment, NULL, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(payload);
    SpawnRequest request = SpawnRequest__overlay(payload);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT64(1, result);
        },
        error_test_fail()
    );
    PosixExecPayload__dispose(payload);

    TEST_ASSERT_EQUAL_UINT64(pid, parent->pid);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/dynamic"), parent->name);
    ProcessContextPtr context = Process__required_context(parent);
    UserAllocation startup = allocation_at(parent, 0);
    Address user_base = startup.address.virtual_addr;
    Address kernel_base = startup.address.kernel_addr.virtual_addr;
    ConstExecPayloadPtr startup_payload = ptr(kernel_base + (context->x[7] - user_base), ExecPayload);
    TEST_ASSERT_EQUAL_STRING("/dynamic", ExecPayload__path(startup_payload));
    TEST_ASSERT_EQUAL_STRING("/work", ExecPayload__current_directory(startup_payload));
    TEST_ASSERT_EQUAL_UINT(2, filesystem.opens);
    TEST_ASSERT_EQUAL_BSTRING(bstr(WUFF_INTERPRETER_PATH), filesystem.second_path);
}

void test_exec_overlay_replaces_the_calling_image_and_preserves_process_identity(void)
{
    ProcessID pid = parent->pid;
    ProcessPageTable old_page_table = parent->page_table;
    ProcessContextPtr old_context = Process__required_context(parent);
    ProcessTable before = Scheduler__process_snapshot(kernel->scheduler);
    Process__set_user_identity(parent);
    parent->startup_stdio =
        SpawnStdio__new(IO_ENDPOINT_KEYBOARD_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    ProcessSignalAction caught = {
        .handler = 0x12340000,
        .restorer = 0x56780000,
    };
    ProcessSignalAction ignored = {
        .handler = PROCESS_SIGNAL_IGNORE_HANDLER,
    };
    ProcessSignalMask blocked = 1ULL << (PROCESS_SIGNAL_USER_1 - 1);
    match_result(Process__set_signal_action(parent, PROCESS_SIGNAL_INTERRUPT, &caught),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Process__set_signal_action(parent, PROCESS_SIGNAL_CHILD, &ignored),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(Process__set_signal_mask(parent, PROCESS_SIGNAL_MASK_SET, blocked),
        success_do_nothing(),
                 error_test_fail()
    );
    Process__queue_signal(parent, PROCESS_SIGNAL_USER_1);
    char * arguments[] = {"hello_world", "overlay", NULL};
    char * environment[] = {"PATH=/sbin", "HOME=/", NULL};
    ExecPayloadPtr payload =
        PosixExecPayload__new("/sbin/hello_world", "/work", arguments, environment, NULL, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(payload);
    SpawnRequest request = SpawnRequest__overlay(payload);
    Optional(ProcessPtr) caller = Some(ProcessPtr, parent);

    match_result(
        FakeExecService__spawn(&filesystem, kernel, &caller, (Anything)&request),
        success(result)
        {
            TEST_ASSERT_EQUAL_UINT64(2, result);
        },
        error_test_fail()
    );
    PosixExecPayload__dispose(payload);

    ProcessTable after = Scheduler__process_snapshot(kernel->scheduler);
    TEST_ASSERT_EQUAL_PTR(parent, unwrap_optional(Scheduler__find_process_by_pid(kernel->scheduler, pid), NULL));
    TEST_ASSERT_EQUAL_UINT64(ProcessTable__len(&before), ProcessTable__len(&after));
    TEST_ASSERT_EQUAL_UINT64(pid, parent->pid);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_USER_USER, parent->user_id);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_GROUP_USER, parent->group_id);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/hello_world"), parent->name);
    TEST_ASSERT_NOT_EQUAL(ProcessPageTable__root(&old_page_table).virtual_addr,
                          ProcessPageTable__root(&parent->page_table).virtual_addr);
    TEST_ASSERT_TRUE(old_context != Process__required_context(parent));
    TEST_ASSERT_EQUAL_PTR(parent, active_address_space_process());
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_KEYBOARD_INPUT, SpawnStdio__stdin_fd(parent->startup_stdio));
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stdout_fd(parent->startup_stdio));
    ProcessContextPtr context = Process__required_context(parent);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_KEYBOARD_INPUT, context->x[4]);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, context->x[5]);
    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, context->x[6]);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_DEFAULT_HANDLER,
                             Process__signal_action(parent, PROCESS_SIGNAL_INTERRUPT).handler);
    TEST_ASSERT_EQUAL_UINT64(PROCESS_SIGNAL_IGNORE_HANDLER,
                             Process__signal_action(parent, PROCESS_SIGNAL_CHILD).handler);
    TEST_ASSERT_EQUAL_UINT64(blocked, Process__signal_mask(parent));
    TEST_ASSERT_EQUAL_UINT64(blocked, Process__pending_signal_mask(parent));

    UserAllocation startup = allocation_at(parent, 0);
    AddressPtr argv = ptr(startup.address.kernel_addr.virtual_addr, Address);
    Address user_base = startup.address.virtual_addr;
    Address kernel_base = startup.address.kernel_addr.virtual_addr;
    AddressPtr envp = argv + 3;
    TEST_ASSERT_EQUAL(0, argv[2]);
    TEST_ASSERT_EQUAL_STRING("hello_world", ptr(kernel_base + (argv[0] - user_base), char));
    TEST_ASSERT_EQUAL_STRING("overlay", ptr(kernel_base + (argv[1] - user_base), char));
    TEST_ASSERT_EQUAL(0, envp[2]);
    TEST_ASSERT_EQUAL_STRING("PATH=/sbin", ptr(kernel_base + (envp[0] - user_base), char));
    TEST_ASSERT_EQUAL_STRING("HOME=/", ptr(kernel_base + (envp[1] - user_base), char));
    ConstExecPayloadPtr startup_payload = ptr(kernel_base + (context->x[7] - user_base), ExecPayload);
    TEST_ASSERT_EQUAL_STRING("/work", ExecPayload__current_directory(startup_payload));
}

void test_exec_overlay_rejects_invalid_payload_without_changing_the_caller(void)
{
    ProcessID pid = parent->pid;
    KernelAddress page_table = ProcessPageTable__root(&parent->page_table);
    ProcessContextPtr context = Process__required_context(parent);
    ProcessTable before = Scheduler__process_snapshot(kernel->scheduler);
    char * arguments[] = {"hello_world", NULL};
    char * environment[] = {"PATH=/sbin", NULL};
    ExecPayloadPtr payload = PosixExecPayload__new("/sbin/hello_world", "/", arguments, environment, NULL, 0, NULL, 0);
    TEST_ASSERT_NOT_NULL(payload);
    payload->environment_offset = payload->size;
    SpawnRequest request = SpawnRequest__overlay(payload);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
    PosixExecPayload__dispose(payload);

    ProcessTable after = Scheduler__process_snapshot(kernel->scheduler);
    TEST_ASSERT_EQUAL_UINT64(ProcessTable__len(&before), ProcessTable__len(&after));
    TEST_ASSERT_EQUAL_PTR(parent, unwrap_optional(Scheduler__find_process_by_pid(kernel->scheduler, pid), NULL));
    TEST_ASSERT_EQUAL_UINT64(page_table.virtual_addr, ProcessPageTable__root(&parent->page_table).virtual_addr);
    TEST_ASSERT_EQUAL_PTR(context, Process__required_context(parent));
    TEST_ASSERT_TRUE(is_nothing(parent->exec_load));
}

void test_exec_request_handler_can_spawn_as_user(void)
{
    Process__set_hund_identity(parent);
    BString username = bstring("user");
    BString password = bstring("user");
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("user"), &bstr("user"));
    AuthTicketID ticket_id = unwrap_result(auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request("/sbin/shell", P_NOWAIT, ticket_id, SPAWN_STDIO_DEFAULT);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_GREATER_THAN(0, code);
        },
        error_test_fail()
    );
}

void test_exec_request_handler_rejects_reused_auth_ticket(void)
{
    BString username = bstring("user");
    BString password = bstring("user");
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("user"), &bstr("user"));
    AuthTicketID ticket_id = unwrap_result(auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request("/sbin/shell", P_NOWAIT, ticket_id, SPAWN_STDIO_DEFAULT);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_GREATER_THAN(0, code);
        },
        error_test_fail()
    );

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(AUTH_TICKET_CONSUMED, code);
        }
    );
}

void test_exec_request_handler_can_spawn_root_shell_with_auth_ticket(void)
{
    Process__set_hund_identity(parent);
    Result(AuthTicketID) auth_result = Auth__authenticate(&kernel->auth, parent, &bstr("root"), &bstr("root"));
    AuthTicketID ticket_id = unwrap_result(auth_result, AUTH_NO_TICKET);

    SpawnRequest request = test_spawn_request("/sbin/shell", P_NOWAIT, ticket_id, SPAWN_STDIO_DEFAULT);

    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(code)
        {
            TEST_ASSERT_GREATER_THAN(0, code);
        },
        error_test_fail()
    );
}

void test_exec_request_handler_can_spawn_with_explicit_terminal_endpoints(void)
{
    SpawnStdio stdio =
        SpawnStdio__new(IO_ENDPOINT_KEYBOARD_INPUT, IO_ENDPOINT_CONSOLE_OUTPUT, IO_ENDPOINT_CONSOLE_OUTPUT);
    SpawnRequest request = test_spawn_request("/sbin/shell", P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, stdio);
    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success(pid)
        {
            match_optional(
                Scheduler__find_process_by_pid(kernel->scheduler, pid),
                some(child)
                {
                    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_KEYBOARD_INPUT, SpawnStdio__stdin_fd(child->startup_stdio));
                    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stdout_fd(child->startup_stdio));
                    TEST_ASSERT_EQUAL_INT64(IO_ENDPOINT_CONSOLE_OUTPUT, SpawnStdio__stderr_fd(child->startup_stdio));
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_exec_request_rejects_unsupported_handle_inheritance(void)
{
    SpawnRequest request =
        test_spawn_request("/sbin/shell", P_NOWAIT, SPAWN_INHERIT_AUTH_TICKET, SpawnStdio__new(7, 8, 9));
    match_result(
        ExecRequest__handler(kernel, Some(ProcessPtr, parent), (Anything)&request, 0, 0, 0, 0),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(SPAWN_FAILED, code);
        }
    );
}

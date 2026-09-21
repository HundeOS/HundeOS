#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/spawn.h"

typedef struct SpawnCall
{
    SpawnMode mode;
    BString path;
    SpawnArguments arguments;
} SpawnCall;

static SpawnCall last_spawn_call;
static uint64_t spawn_call_count;
static SpawnResult next_spawn_result = SPAWN_SUCCESS;
static BString printed_output;
static bool exit_called;
static int exit_code;
static BString console_input;
static uint64_t console_input_pos;

void fake_exit(int code);

#define exit fake_exit
#ifndef FIXED_ARRAY_ALLOC
#define FIXED_ARRAY_ALLOC(size) malloc(size)
#define FIXED_ARRAY_ERROR(message) BString__new(message)
#define __TEST_SHELL_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif
FixedArrayBodyDefine(SpawnArguments);
#ifdef __TEST_SHELL_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#undef FIXED_ARRAY_ALLOC
#undef FIXED_ARRAY_ERROR
#undef __TEST_SHELL_SPAWN_FIXED_ARRAY_ALLOC_DEFINED
#endif
#include "../../../../user/commands/shell/interpreter.c"
#include "../../../../user/commands/shell/input_loop.c"
#undef exit

typedef struct TestInputStream
{
    InputStream input;
    BString line;
} TestInputStream;

void setUp(void)
{
    spawn_call_count = 0;
    next_spawn_result = SPAWN_SUCCESS;
    last_spawn_call = (SpawnCall){
        .path = empty_bstr(),
        .arguments = SpawnArguments__new(),
    };
    printed_output = empty_bstr();
    exit_called = false;
    exit_code = -1;
    console_input = empty_bstr();
    console_input_pos = 0;
}

void tearDown(void)
{
}

static BString TestString__from_nullable(const char * value)
{
    if (value == NULL)
    {
        return empty_bstr();
    }

    return BString__new(value);
}

static void assert_command_argument(CommandPtr command, StringPos index, ConstBStringPtr expected)
{
    match_optional(
        CommandArguments__at(&command->args, index),
        some(argument)
        {
            TEST_ASSERT_EQUAL_BSTRING(*expected, argument);
        },
        none_test_fail()
    );
}

static void assert_spawn_argument(StringPos index, ConstBStringPtr expected)
{
    match_optional(
        SpawnArguments__at(&last_spawn_call.arguments, index),
        some(argument)
        {
            TEST_ASSERT_EQUAL_BSTRING(*expected, argument);
        },
        none_test_fail()
    );
}

static BString TestInputStream__read_line(InputStreamPtr self, bool echo)
{
    TestInputStream * input = (TestInputStream *)self;
    return input->line;
}

static void set_console_input(const char * value)
{
    console_input = BString__new(value);
    console_input_pos = 0;
}

SpawnResult spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                  OptionalPtr(BString) arg2)
{
    spawn_call_count++;
    last_spawn_call.mode = mode;
    last_spawn_call.path = *path;
    last_spawn_call.arguments = SpawnArguments__new();
    match_optional(*arg0,
        some(value)
        {
            SpawnArguments__push(&last_spawn_call.arguments, value);
        },
        none_do_nothing()
    );
    match_optional(*arg1,
        some(value)
        {
            SpawnArguments__push(&last_spawn_call.arguments, value);
        },
        none_do_nothing()
    );
    match_optional(*arg2,
        some(value)
        {
            SpawnArguments__push(&last_spawn_call.arguments, value);
        },
        none_do_nothing()
    );
    return next_spawn_result;
}

SpawnResult spawn_argv(SpawnMode mode, ConstBStringPtr path, ConstSpawnArgumentsPtr arguments)
{
    spawn_call_count++;
    last_spawn_call.mode = mode;
    last_spawn_call.path = *path;
    last_spawn_call.arguments = *arguments;
    return next_spawn_result;
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString text = BString__vsprintf(format, args);
    va_end(args);
    IO__print(&text);
    return (int)BString__length(&text);
}

void IO__print(ConstBStringPtr message)
{
    const char * str = BString__cstr(message);
    BString text = BString__new(str);
    BString next = BString__append(&printed_output, &text);
    printed_output = next;
}

ByteCount console_read(char * buffer, uint64_t size)
{
    if (size == 0 || console_input_pos >= (uint64_t)console_input.len)
    {
        return 0;
    }

    buffer[0] = console_input.data[console_input_pos];
    console_input_pos++;
    return 1;
}

BString Console__read_line(bool echo)
{
    return empty_bstr();
}

void fake_exit(int code)
{
    exit_called = true;
    exit_code = code;
}

void test_interpreter_parse_returns_empty_command_for_empty_input(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("   \n");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_TRUE(BString__is_empty(&command.name));
    TEST_ASSERT_EQUAL_UINT64(0, CommandArguments__len(&command.args));
}

void test_interpreter_parse_returns_command_value(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write /mnt/fs1/out.txt \"hello world\"");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_BSTRING(bstr("write"), command.name);
    TEST_ASSERT_EQUAL_UINT64(2, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, &bstr("/mnt/fs1/out.txt"));
    assert_command_argument(&command, 1, &bstr("hello world"));
}

void test_interpreter_parse_supports_empty_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write \"\" tail");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_BSTRING(bstr("write"), command.name);
    TEST_ASSERT_EQUAL_UINT64(2, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, &bstr(""));
    assert_command_argument(&command, 1, &bstr("tail"));
}

void test_interpreter_parse_preserves_root_path_command_name(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("/sbin/cat /mnt/fs1/test.txt");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/cat"), command.name);
    TEST_ASSERT_EQUAL_UINT64(1, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, &bstr("/mnt/fs1/test.txt"));
}

void test_interpreter_parse_collects_more_than_spawn_limit_args(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cmd a b c d");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_BSTRING(bstr("cmd"), command.name);
    TEST_ASSERT_EQUAL_UINT64(4, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, &bstr("a"));
    assert_command_argument(&command, 3, &bstr("d"));
}

void test_interpreter_execute_returns_success_without_spawn_for_empty_command(void)
{
    Interpreter interpreter = Interpreter__new();
    Command command = Command__new();

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__execute(&interpreter, &command));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_call_count);
}

void test_interpreter_execute_spawns_command_with_args(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cat \"This is a long filename.txt\"");
    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__execute(&interpreter, &command));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_WAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/cat"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(1, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("This is a long filename.txt"));
}

void test_interpreter_execute_spawns_background_command_with_same_args(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cat test.txt &");
    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_TRUE(command.background);
    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__execute(&interpreter, &command));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_NOWAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/cat"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(1, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("test.txt"));
}

void test_interpreter_execute_spawns_command_with_more_than_three_args(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cmd a b c d");
    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__execute(&interpreter, &command));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_WAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/cmd"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(4, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("a"));
    assert_spawn_argument(1, &bstr("b"));
    assert_spawn_argument(2, &bstr("c"));
    assert_spawn_argument(3, &bstr("d"));
}

void test_interpreter_execute_accepts_background_spawn_pid_as_success(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("socket_test listen socket://proc/demo &");
    Command command = Interpreter__parse(&interpreter, &input);
    next_spawn_result = 25;

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__execute(&interpreter, &command));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_NOWAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/socket_test"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(2, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("listen"));
    assert_spawn_argument(1, &bstr("socket://proc/demo"));
    TEST_ASSERT_EQUAL_BSTRING(bstr(""), printed_output);
}

void test_interpreter_parse_does_not_pass_background_operator_as_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cat test.txt &");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_TRUE(command.background);
    TEST_ASSERT_EQUAL_UINT64(1, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, &bstr("test.txt"));
}

void test_interpreter_execute_fails_cleanly_when_too_many_args_are_supplied(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cmd a b c d e f g h i j k l m n o p q");
    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL(COMMAND_FAILED, Interpreter__execute(&interpreter, &command));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_call_count);
}

void test_shell_run_once_returns_exit_for_exit_command(void)
{
    TestInputStream input = {.input = {.read_line = TestInputStream__read_line}, .line = bstring("exit")};
    Shell shell = Shell__new(&input.input);

    TEST_ASSERT_EQUAL(EXIT, Shell__run_once(&shell));
    TEST_ASSERT_TRUE(exit_called);
    TEST_ASSERT_EQUAL_INT(0, exit_code);
    TEST_ASSERT_EQUAL_BSTRING(bstr("\n> \nShell exiting\n"), printed_output);
}

void test_shell_run_once_reports_command_failed(void)
{
    TestInputStream input = {.input = {.read_line = TestInputStream__read_line}, .line = bstring("cat missing")};
    Shell shell = Shell__new(&input.input);
    next_spawn_result = SPAWN_FAILED;

    TEST_ASSERT_EQUAL(COMMAND_FAILED, Shell__run_once(&shell));
    TEST_ASSERT_EQUAL_BSTRING(bstr("\n> \nresult = -128\nCommand failed\n"), printed_output);
}

void test_shell_run_once_stores_non_empty_history_entries(void)
{
    TestInputStream input = {.input = {.read_line = TestInputStream__read_line}, .line = bstring("cat test.txt")};
    Shell shell = Shell__new(&input.input);

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Shell__run_once(&shell));
    TEST_ASSERT_EQUAL_UINT64(1, ShellHistory__len(&shell.history));

    match_optional(
        ShellHistory__at(&shell.history, 0),
        some(entry)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("cat test.txt"), entry);
        },
        none_test_fail()
    );
}

void test_interpreter_interpret_groups_double_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cat \"This is a long filename.txt\"");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_WAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/cat"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(1, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("This is a long filename.txt"));
}

void test_interpreter_interpret_mixes_quoted_and_unquoted_arguments(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write /mnt/fs1/out.txt \"hello world\"");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/write"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(2, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr("/mnt/fs1/out.txt"));
    assert_spawn_argument(1, &bstr("hello world"));
}

void test_interpreter_interpret_supports_empty_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write \"\" tail");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/write"), last_spawn_call.path);
    TEST_ASSERT_EQUAL_UINT64(2, SpawnArguments__len(&last_spawn_call.arguments));
    assert_spawn_argument(0, &bstr(""));
    assert_spawn_argument(1, &bstr("tail"));
}

void test_interpreter_interpret_ignores_empty_input(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("   \n");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_call_count);
}

void test_console_input_stream_recalls_previous_history_entry_with_up_arrow(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    Shell shell = Shell__new(&input.input);
    ConsoleInputStream__bind_history(&input, &shell.history);
    ShellHistory__push(&shell.history, bstring("first"));
    ShellHistory__push(&shell.history, bstring("second"));
    set_console_input("\x1b[A\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("second"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("\r\033[2K> second"), printed_output);
}

void test_console_input_stream_restores_current_draft_with_down_arrow(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    Shell shell = Shell__new(&input.input);
    ConsoleInputStream__bind_history(&input, &shell.history);
    ShellHistory__push(&shell.history, bstring("first"));
    ShellHistory__push(&shell.history, bstring("second"));
    set_console_input("par\x1b[A\x1b[B\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("par"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("par\r\033[2K> second\r\033[2K> par"), printed_output);
}

void test_console_input_stream_inserts_at_cursor_after_left_arrow(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    set_console_input("ac\x1b[DX\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("aXc"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("ac\r\033[2K> ac\033[1D\r\033[2K> aXc\033[1D"), printed_output);
}

void test_console_input_stream_backspace_removes_before_cursor(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    set_console_input("abc\x1b[D\x1b[D\x7f\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("bc"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("abc\r\033[2K> abc\033[1D\r\033[2K> abc\033[2D\r\033[2K> bc\033[2D"),
                              printed_output);
}

void test_console_input_stream_recalled_history_line_can_move_and_insert(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    Shell shell = Shell__new(&input.input);
    ConsoleInputStream__bind_history(&input, &shell.history);
    ShellHistory__push(&shell.history, bstring("hello"));
    set_console_input("\x1b[A\x1b[DX\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("hellXo"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("\r\033[2K> hello\r\033[2K> hello\033[1D\r\033[2K> hellXo\033[1D"), printed_output);
}

void test_console_input_stream_appends_without_full_redraw(void)
{
    ConsoleInputStream input = ConsoleInputStream__new();
    set_console_input("hello\n");

    BString line = ConsoleInputStream__read_line(&input.input, true);

    TEST_ASSERT_EQUAL_BSTRING(bstr("hello"), line);
    TEST_ASSERT_EQUAL_BSTRING(bstr("hello"), printed_output);
}

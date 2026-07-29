#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/spawn.h"

typedef struct SpawnCall
{
    SpawnMode mode;
    BString path;
    BString arg0;
    BString arg1;
    BString arg2;
}
SpawnCall;

static SpawnCall last_spawn_call;
static uint64_t spawn_call_count;
static SpawnResult next_spawn_result = SPAWN_SUCCESS;
static BString printed_output;
static bool exit_called;
static int exit_code;

#define exit fake_exit
#include "../../../../user/commands/shell/interpreter.c"
#include "../../../../user/commands/shell/input_loop.c"
#undef exit

typedef struct TestInputStream
{
    InputStream input;
    BString line;
}
TestInputStream;

void setUp(void)
{
    spawn_call_count = 0;
    next_spawn_result = SPAWN_SUCCESS;
    last_spawn_call = (SpawnCall) {
        .path = EMPTY_BSTRING,
        .arg0 = EMPTY_BSTRING,
        .arg1 = EMPTY_BSTRING,
        .arg2 = EMPTY_BSTRING,
    };
    printed_output = EMPTY_BSTRING;
    exit_called = false;
    exit_code = -1;
}

void tearDown(void)
{
}

static BString TestString__from_nullable(const char * value)
{
    if (value == NULL)
    {
        return EMPTY_BSTRING;
    }

    return BString__new(value);
}

static void assert_command_argument(CommandPtr command, StringPos index, const char * expected)
{
    Match(BString, CommandArguments__at(&command->args, index))
        IsSome(argument, TEST_ASSERT_EQUAL_STRING(expected, BString__cstr(&argument)))
        IsNone(TEST_FAIL())
    End
}

static BString TestInputStream__read_line(InputStreamPtr self, bool echo)
{
    TestInputStream * input = (TestInputStream *) self;
    return input->line;
}

SpawnResult spawn(SpawnMode mode, const char* path, const char* arg0, const char* arg1, const char* arg2)
{
    spawn_call_count++;
    last_spawn_call.mode = mode;
    last_spawn_call.path = TestString__from_nullable(path);
    last_spawn_call.arg0 = TestString__from_nullable(arg0);
    last_spawn_call.arg1 = TestString__from_nullable(arg1);
    last_spawn_call.arg2 = TestString__from_nullable(arg2);
    return next_spawn_result;
}

int printf(const char * format, ...)
{
    return 0;
}

void print(const char * str)
{
    BString text = BString__new(str);
    BString next = BString__append(&printed_output, &text);
    printed_output = next;
}

BString Console__read_line(bool echo)
{
    return EMPTY_BSTRING;
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

    TEST_ASSERT_EQUAL_STRING("write", BString__cstr(&command.name));
    TEST_ASSERT_EQUAL_UINT64(2, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, "/mnt/fs1/out.txt");
    assert_command_argument(&command, 1, "hello world");
}

void test_interpreter_parse_supports_empty_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write \"\" tail");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_STRING("write", BString__cstr(&command.name));
    TEST_ASSERT_EQUAL_UINT64(2, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, "");
    assert_command_argument(&command, 1, "tail");
}

void test_interpreter_parse_preserves_root_path_command_name(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("/ebin/cat /mnt/fs1/test.txt");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_STRING("/ebin/cat", BString__cstr(&command.name));
    TEST_ASSERT_EQUAL_UINT64(1, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, "/mnt/fs1/test.txt");
}

void test_interpreter_parse_collects_more_than_spawn_limit_args(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cmd a b c d");

    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL_STRING("cmd", BString__cstr(&command.name));
    TEST_ASSERT_EQUAL_UINT64(4, CommandArguments__len(&command.args));
    assert_command_argument(&command, 0, "a");
    assert_command_argument(&command, 3, "d");
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
    TEST_ASSERT_EQUAL_STRING("/ebin/cat", BString__cstr(&last_spawn_call.path));
    TEST_ASSERT_EQUAL_STRING("This is a long filename.txt", BString__cstr(&last_spawn_call.arg0));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg1));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg2));
}

void test_interpreter_execute_fails_cleanly_when_too_many_args_are_supplied(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cmd a b c d");
    Command command = Interpreter__parse(&interpreter, &input);

    TEST_ASSERT_EQUAL(COMMAND_FAILED, Interpreter__execute(&interpreter, &command));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_call_count);
}

void test_shell_run_once_returns_exit_for_exit_command(void)
{
    TestInputStream input = {
        .input = {
            .read_line = TestInputStream__read_line
        },
        .line = bstring("exit")
    };
    Shell shell = Shell__new(&input.input);

    TEST_ASSERT_EQUAL(EXIT, Shell__run_once(&shell));
    TEST_ASSERT_TRUE(exit_called);
    TEST_ASSERT_EQUAL_INT(0, exit_code);
    TEST_ASSERT_EQUAL_STRING("\n> \nShell exiting\n", BString__cstr(&printed_output));
}

void test_shell_run_once_reports_command_failed(void)
{
    TestInputStream input = {
        .input = {
            .read_line = TestInputStream__read_line
        },
        .line = bstring("cat missing")
    };
    Shell shell = Shell__new(&input.input);
    next_spawn_result = SPAWN_FAILED;

    TEST_ASSERT_EQUAL(COMMAND_FAILED, Shell__run_once(&shell));
    TEST_ASSERT_EQUAL_STRING("\n> \nresult = -128\nCommand failed\n", BString__cstr(&printed_output));
}

void test_interpreter_interpret_groups_double_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("cat \"This is a long filename.txt\"");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL(P_WAIT, last_spawn_call.mode);
    TEST_ASSERT_EQUAL_STRING("/ebin/cat", BString__cstr(&last_spawn_call.path));
    TEST_ASSERT_EQUAL_STRING("This is a long filename.txt", BString__cstr(&last_spawn_call.arg0));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg1));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg2));
}

void test_interpreter_interpret_mixes_quoted_and_unquoted_arguments(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write /mnt/fs1/out.txt \"hello world\"");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/ebin/write", BString__cstr(&last_spawn_call.path));
    TEST_ASSERT_EQUAL_STRING("/mnt/fs1/out.txt", BString__cstr(&last_spawn_call.arg0));
    TEST_ASSERT_EQUAL_STRING("hello world", BString__cstr(&last_spawn_call.arg1));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg2));
}

void test_interpreter_interpret_supports_empty_quoted_argument(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("write \"\" tail");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));

    TEST_ASSERT_EQUAL_UINT64(1, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/ebin/write", BString__cstr(&last_spawn_call.path));
    TEST_ASSERT_EQUAL_STRING("", BString__cstr(&last_spawn_call.arg0));
    TEST_ASSERT_EQUAL_STRING("tail", BString__cstr(&last_spawn_call.arg1));
}

void test_interpreter_interpret_ignores_empty_input(void)
{
    Interpreter interpreter = Interpreter__new();
    BString input = bstring("   \n");

    TEST_ASSERT_EQUAL(COMMAND_SUCCESS, Interpreter__interpret(&interpreter, &input));
    TEST_ASSERT_EQUAL_UINT64(0, spawn_call_count);
}

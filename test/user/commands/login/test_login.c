#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/auth.h"
#include "../../../../common_includes/syscalls/process.h"
#include "../../../../common_includes/syscalls/spawn.h"

static SpawnMode last_mode;
static AuthTicketID last_auth_ticket_id;
static char last_path[64];
static SpawnResult next_spawn_result;
static char last_auth_username[64];
static char last_auth_password[64];
static char output[512];
static uint64_t output_position;
static BString console_inputs[8];
static uint64_t console_input_count;
static uint64_t console_input_index;

#define main login_main_under_test
#include "../../../../user/commands/login/main.c"

void setUp(void)
{
    last_mode = 0;
    last_auth_ticket_id = AUTH_NO_TICKET;
    last_path[0] = '\0';
    next_spawn_result = 1;
    last_auth_username[0] = '\0';
    last_auth_password[0] = '\0';
    output_position = 0;
    output[0] = '\0';
    console_input_count = 0;
    console_input_index = 0;
}

void tearDown(void)
{
}

static void copy_text(char * target, uint64_t target_size, const char * source)
{
    uint64_t position = 0;
    while (position + 1 < target_size && source[position] != '\0')
    {
        target[position] = source[position];
        position++;
    }
    target[position] = '\0';
}

static bool contains_text(const char * haystack, const char * needle)
{
    uint64_t haystack_index = 0;
    if (needle[0] == '\0')
    {
        return true;
    }

    while (haystack[haystack_index] != '\0')
    {
        uint64_t needle_index = 0;
        while (needle[needle_index] != '\0' && haystack[haystack_index + needle_index] != '\0' &&
               haystack[haystack_index + needle_index] == needle[needle_index])
        {
            needle_index++;
        }

        if (needle[needle_index] == '\0')
        {
            return true;
        }

        haystack_index++;
    }

    return false;
}

static void queue_console_input(const char * value)
{
    console_inputs[console_input_count++] = BString__new(value);
}

AuthResult auth(ConstBStringPtr username, ConstBStringPtr password)
{
    copy_text(last_auth_username, sizeof(last_auth_username), BString__cstr(username));
    copy_text(last_auth_password, sizeof(last_auth_password), BString__cstr(password));
    if (strcmp(BString__cstr(username), "root") == 0 && strcmp(BString__cstr(password), "root") == 0)
    {
        return 42;
    }
    if (strcmp(BString__cstr(username), "hund") == 0 && strcmp(BString__cstr(password), "hund") == 0)
    {
        return 42;
    }
    if (strcmp(BString__cstr(username), "user") == 0 && strcmp(BString__cstr(password), "user") == 0)
    {
        return 42;
    }
    return AUTH_INVALID_CREDENTIALS;
}

SpawnResult spawn_with_auth(SpawnMode mode, AuthTicketID auth_ticket_id, ConstBStringPtr path,
                            OptionalPtr(BString) arg0, OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    last_mode = mode;
    last_auth_ticket_id = auth_ticket_id;
    copy_text(last_path, sizeof(last_path), BString__cstr(path));
    match_optional(*arg0,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(*arg1,
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(*arg2,
        some_test_fail(),
        none_do_nothing()
    );
    return next_spawn_result;
}

void IO__print(ConstBStringPtr message)
{
    const char * str = BString__cstr(message);
    while (*str != '\0' && output_position + 1 < sizeof(output))
    {
        output[output_position++] = *str++;
    }
    output[output_position] = '\0';
}

int IO__printf(ConstBStringPtr format, ...)
{
    va_list args;
    va_start(args, format);
    BString formatted = BString__vsprintf(format, args);
    va_end(args);
    StringPos written = BString__copy_to(&formatted, output + output_position, sizeof(output) - output_position);
    if (written > 0)
    {
        output_position += (uint64_t)written;
    }
    return (int)written;
}

BString Console__read_line(bool echo)
{
    TEST_ASSERT_LESS_THAN_UINT64(console_input_count, console_input_index);
    return console_inputs[console_input_index++];
}

void test_login_authenticates_user_and_spawns_shell_as_user(void)
{
    queue_console_input("user");
    queue_console_input("user");
    next_spawn_result = -1;

    TEST_ASSERT_EQUAL_INT(1, login_main_under_test());
    TEST_ASSERT_EQUAL_STRING("user", last_auth_username);
    TEST_ASSERT_EQUAL_STRING("user", last_auth_password);
    TEST_ASSERT_EQUAL(P_WAIT, last_mode);
    TEST_ASSERT_EQUAL_UINT64(42, last_auth_ticket_id);
    TEST_ASSERT_EQUAL_STRING("/usr/local/bin/bash", last_path);
    TEST_ASSERT_TRUE(contains_text(output, "login: failed to start bash: -1\n"));
}

void test_login_retries_after_invalid_credentials(void)
{
    queue_console_input("user");
    queue_console_input("bad");
    queue_console_input("root");
    queue_console_input("root");
    next_spawn_result = -1;

    TEST_ASSERT_EQUAL_INT(1, login_main_under_test());
    TEST_ASSERT_EQUAL_UINT64(42, last_auth_ticket_id);
    TEST_ASSERT_TRUE(contains_text(output, "login: invalid credentials\n"));
    TEST_ASSERT_TRUE(contains_text(output, "login: failed to start bash: -1\n"));
}

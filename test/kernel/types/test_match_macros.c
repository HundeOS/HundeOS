#include "unity.h"

#include "../../../common/types/optional.h"
#include "../../../common/types/result.h"
#include "../../../kernel/mutex.h"

typedef enum MatchMacroCode
{
    MATCH_MACRO_OK = 1,
    MATCH_MACRO_FAILED = 2,
} MatchMacroCode;

OptionalDef(MatchMacroCode);
ResultDef(MatchMacroCode);

void setUp(void)
{
}

void tearDown(void)
{
}

void test_match_optional_runs_some_branch(void)
{
    bool saw_some = false;
    bool saw_none = false;

    match_optional(
        Some(MatchMacroCode, MATCH_MACRO_OK),
        some(value)
        {
            saw_some = true;
            TEST_ASSERT_EQUAL(MATCH_MACRO_OK, value);
        },
        none()
        {
            saw_none = true;
        }
    );

    TEST_ASSERT_TRUE(saw_some);
    TEST_ASSERT_FALSE(saw_none);
}

void test_match_optional_runs_none_branch(void)
{
    bool saw_some = false;
    bool saw_none = false;

    match_optional(None(MatchMacroCode),
        some(value)
        {
            saw_some = true;
        },
        none()
        {
            saw_none = true;
        }
    );

    TEST_ASSERT_FALSE(saw_some);
    TEST_ASSERT_TRUE(saw_none);
}

void test_unwrap_optional_parenthesizes_value_for_comparisons(void)
{
    Optional(char) value = Some(char, 's');

    TEST_ASSERT_FALSE(unwrap_optional(value, '\0') == '/');
    TEST_ASSERT_TRUE(unwrap_optional(value, '\0') == 's');
}

void test_match_result_runs_ok_branch(void)
{
    bool saw_ok = false;
    bool saw_error = false;

    match_result(
        Success(MatchMacroCode, MATCH_MACRO_OK),
        success(value)
        {
            saw_ok = true;
            TEST_ASSERT_EQUAL(MATCH_MACRO_OK, value);
        },
        error(code, message)
        {
            saw_error = true;
        }
    );

    TEST_ASSERT_TRUE(saw_ok);
    TEST_ASSERT_FALSE(saw_error);
}

void test_match_result_runs_error_branch(void)
{
    bool saw_ok = false;
    bool saw_error = false;
    BString expected = bstring("failed");

    match_result(
        Error(MatchMacroCode, expected, MATCH_MACRO_FAILED),
        success(value)
        {
            saw_ok = true;
        },
        error(code, message)
        {
            saw_error = true;
            TEST_ASSERT_EQUAL(MATCH_MACRO_FAILED, code);
            TEST_ASSERT_EQUAL_BSTRING(expected, message);
        }
    );

    TEST_ASSERT_FALSE(saw_ok);
    TEST_ASSERT_TRUE(saw_error);
}

void test_unwrap_result_parenthesizes_value_for_comparisons(void)
{
    Result(MatchMacroCode) result = Success(MatchMacroCode, MATCH_MACRO_OK);

    TEST_ASSERT_FALSE(unwrap_result(result, MATCH_MACRO_FAILED) == MATCH_MACRO_FAILED);
    TEST_ASSERT_TRUE(unwrap_result(result, MATCH_MACRO_FAILED) == MATCH_MACRO_OK);
}

void test_critical_section_runs_block(void)
{
    mutex_t lock = MUTEX_INIT;
    int value = 0;

    critical_section(
        lock,
        {
            value = 7;
        }
    );

    TEST_ASSERT_EQUAL(7, value);
}

static int returns_from_critical_section(mutex_t * lock)
{
    critical_section(
        *lock,
        {
            cs_return 11;
        }
    );

    return 0;
}

void test_cs_return_unlocks_before_returning(void)
{
    mutex_t lock = MUTEX_INIT;

    TEST_ASSERT_EQUAL(11, returns_from_critical_section(&lock));
    TEST_ASSERT_TRUE(mutex_trylock(&lock));
    mutex_unlock(&lock);
}

static int exits_from_critical_section(mutex_t * lock)
{
    critical_section(
        *lock,
        {
            cs_exit_then(return 13);
        }
    );

    return 0;
}

void test_cs_exit_then_unlocks_before_returning(void)
{
    mutex_t lock = MUTEX_INIT;

    TEST_ASSERT_EQUAL(13, exits_from_critical_section(&lock));
    TEST_ASSERT_TRUE(mutex_trylock(&lock));
    mutex_unlock(&lock);
}

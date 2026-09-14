#include "unity.h"

#include "types/optional.h"
#include "types/result.h"

typedef uint64_t UnwrapValue;
OptionalDef(UnwrapValue);
ResultDef(UnwrapValue);

static uint64_t evaluations;

static Optional(UnwrapValue) optional_value(void)
{
    evaluations++;
    return Some(UnwrapValue, 17);
}

static Result(UnwrapValue) result_value(void)
{
    evaluations++;
    return Success(UnwrapValue, 23);
}

void setUp(void)
{
    evaluations = 0;
}

void tearDown(void)
{
}

void test_unwrap_optional_evaluates_expression_once(void)
{
    TEST_ASSERT_EQUAL_UINT64(17, unwrap_optional(optional_value(), 0));
    TEST_ASSERT_EQUAL_UINT64(1, evaluations);
}

void test_unwrap_result_evaluates_expression_once(void)
{
    TEST_ASSERT_EQUAL_UINT64(23, unwrap_result(result_value(), 0));
    TEST_ASSERT_EQUAL_UINT64(1, evaluations);
}

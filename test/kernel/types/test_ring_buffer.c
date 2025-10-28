#include "unity.h"
#include "../../../kernel/types/ring_buffer.h"
#include "../../../kernel/types/result.h"

RingBufferDefine(10, SmallBuffer)
RingBufferBodyDefine(SmallBuffer)

void setUp(void)
{

}

void tearDown(void)
{

}

void test_ring_buffer_push_and_pop(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 10; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (Char c = 0; c < 10; c++)
    {
        ResultMatch(Char, SmallBuffer__pop(&buffer))
            IsError(code, message, TEST_FAIL())
            IsSuccess(value, TEST_ASSERT_EQUAL_INT(c, value));
        End
    }
}

#define NUM_ITEMS 10

void test_ring_buffer_drain_without_wrap_around()
{
    SmallBuffer buffer = SmallBuffer__new();
    Char drain[NUM_ITEMS];

    Char expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    ResultMatch(ItemCount, SmallBuffer__drain(&buffer, drain, NUM_ITEMS))
        IsSuccess(num,
        {
            TEST_ASSERT_EQUAL_INT(10, num);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, drain, NUM_ITEMS);
        });
        IsError(code, message, TEST_FAIL());
    End
}

void test_ring_buffer_drain_with_wrap_around()
{
    SmallBuffer buffer = SmallBuffer__new();
    Char drain[NUM_ITEMS];

    Char expected[] = {5, 6, 7, 8, 9, 0, 1, 2, 3, 4};

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        ResultMatch(Char, SmallBuffer__pop(&buffer))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    ResultMatch(ItemCount, SmallBuffer__drain(&buffer, drain, NUM_ITEMS))
        IsSuccess(num,
        {
            TEST_ASSERT_EQUAL_INT(10, num);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, drain, NUM_ITEMS);
        });
        IsError(code, message, TEST_FAIL());
    End
}

void test_ring_buffer_push_overflow(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 10; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    ResultMatch(Pos, SmallBuffer__push(&buffer, 100))
        IsSuccess(value, TEST_FAIL());
    IsError(code, message, TEST_ASSERT_EQUAL_INT(RING_BUFFER_OVERFLOW, code));
    End
}

void test_ring_buffer_pop_underflow(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 5; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (Char c = 0; c < 5; c++)
    {
        ResultMatch(Char, SmallBuffer__pop(&buffer))
            IsSuccess(val, TEST_ASSERT_EQUAL_INT(c, val))
            IsError(code, message, TEST_FAIL())
        End
    }

    ResultMatch(Char, SmallBuffer__pop(&buffer))
        IsSuccess(val, TEST_FAIL())
        IsError(code, message, TEST_ASSERT_EQUAL_INT(RING_BUFFER_UNDERFLOW, code))
    End
}
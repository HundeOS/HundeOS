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


void test_ring_buffer_push_all_when_in_bounds(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    Char data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    ResultMatch(Pos, SmallBuffer__push_all(&buffer, data, sizeof(data)))
        IsError(code, message, TEST_FAIL())
    End

    for (Char c = 0; c < 10; c++)
    {
        ResultMatch(Char, SmallBuffer__pop(&buffer))
            IsError(code, message, TEST_FAIL())
            IsSuccess(value, TEST_ASSERT_EQUAL_INT(c, value));
        End
    }
}

void test_ring_buffer_push_all_when_beyond_bounds(void)
{
    SmallBuffer buffer = SmallBuffer__new();
    ResultMatch(Pos, SmallBuffer__push(&buffer, 11))
        IsError(code, message, TEST_FAIL())
    End

    Char data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    ResultMatch(Pos, SmallBuffer__push_all(&buffer, data, sizeof(data)))
        IsSuccess(pos, TEST_FAIL())
    End
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

void test_ring_buffer_reset()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    ResultMatch(NoType, SmallBuffer__reset(&buffer))
        IsSuccess(_,
        {
            TEST_ASSERT_EQUAL_INT(0, SmallBuffer__item_count(&buffer));
        });
        IsError(code, message, TEST_FAIL());
    End
}

void test_ring_buffer_item_count_not_cycled()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (int i = 0; i < NUM_ITEMS / 2; i++)
    {
        ResultMatch(Char, SmallBuffer__pop(&buffer))
            IsError(code, message, TEST_FAIL())
        End
    }

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS / 2, count);
}

void test_ring_buffer_item_count_when_cycled()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    for (int i = 0; i < NUM_ITEMS / 2; i++)
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

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS, count);
}

void test_ring_buffer_item_count()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS, count);
}

void test_ring_buffer_free_space()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 5; c++)
    {
        ResultMatch(Pos, SmallBuffer__push(&buffer, c))
            IsError(code, message, TEST_FAIL())
        End
    }

    size_t free_space = SmallBuffer__free_space(&buffer);

    TEST_ASSERT_EQUAL_INT(5, free_space);
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
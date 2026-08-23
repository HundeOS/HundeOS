#include "unity.h"
#include "../../../kernel/types/ring_buffer.h"
#include "../../../kernel/types/result.h"

RingBufferDefine(10, SmallBuffer)
RingBufferBodyDefine(SmallBuffer)

RingBufferDefine(4, StackOnlyBuffer)
RingBufferStackBodyDefine(StackOnlyBuffer)

#define NUM_ITEMS 10

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
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < 10; c++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(value) { TEST_ASSERT_EQUAL_INT(c, value); }, error(code, message) { TEST_FAIL(); })
    }
}

void test_ring_buffer_stack_body_define_provides_new_push_and_pop(void)
{
    StackOnlyBuffer buffer = StackOnlyBuffer__new();

    match_result(Pos, StackOnlyBuffer__push(&buffer, 'a'), success(_) {}, error(code, message) { TEST_FAIL(); })

    match_result(Char, StackOnlyBuffer__pop(&buffer), success(value) { TEST_ASSERT_EQUAL_UINT8('a', value); }, error(code, message) { TEST_FAIL(); })
}

void test_ring_buffer_iterate_visits_items_without_consuming(void)
{
    SmallBuffer buffer = SmallBuffer__new();
    Char visited[4] = { 0 };
    size_t count = 0;

    for (Char c = 0; c < 4; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    RingBufferIterate(SmallBuffer, buffer, index, value)
        visited[count] = value;
        count++;
    }

    TEST_ASSERT_EQUAL_UINT64(4, count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((Char[]) { 0, 1, 2, 3 }), visited, 4);
    TEST_ASSERT_EQUAL_UINT64(4, SmallBuffer__item_count(&buffer));
}

void test_ring_buffer_iterate_visits_wrapped_items_in_fifo_order(void)
{
    SmallBuffer buffer = SmallBuffer__new();
    Char visited[10] = { 0 };
    size_t count = 0;

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    RingBufferPtrIterate(SmallBuffer, &buffer, index, value)
        visited[count] = value;
        count++;
    }

    TEST_ASSERT_EQUAL_UINT64(NUM_ITEMS, count);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(((Char[]) { 5, 6, 7, 8, 9, 0, 1, 2, 3, 4 }), visited, NUM_ITEMS);
}

void test_ring_buffer_iterate_empty_buffer_does_not_enter_loop(void)
{
    SmallBuffer buffer = SmallBuffer__new();
    size_t count = 0;

    RingBufferIterate(SmallBuffer, buffer, index, value)
        count++;
    }

    TEST_ASSERT_EQUAL_UINT64(0, count);
}


void test_ring_buffer_push_all_when_in_bounds(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    Char data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    match_result(Pos, SmallBuffer__push_all(&buffer, data, sizeof(data)), success(_) {}, error(code, message) { TEST_FAIL(); })

    for (Char c = 0; c < 10; c++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(value) { TEST_ASSERT_EQUAL_INT(c, value); }, error(code, message) { TEST_FAIL(); })
    }
}

void test_ring_buffer_push_all_when_beyond_bounds(void)
{
    SmallBuffer buffer = SmallBuffer__new();
    match_result(Pos, SmallBuffer__push(&buffer, 11), success(_) {}, error(code, message) { TEST_FAIL(); })

    Char data[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    match_result(Pos, SmallBuffer__push_all(&buffer, data, sizeof(data)), success(pos) { TEST_FAIL(); }, error(code, message) {})
}

void test_ring_buffer_drain_without_wrap_around()
{
    SmallBuffer buffer = SmallBuffer__new();
    Char drain[NUM_ITEMS];

    Char expected[] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    match_result(ItemCount, SmallBuffer__drain(&buffer, drain, NUM_ITEMS), success(num) {
            TEST_ASSERT_EQUAL_INT(10, num);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, drain, NUM_ITEMS);
        }, error(code, message) { TEST_FAIL(); })
}

void test_ring_buffer_reset()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    match_result(NoType, SmallBuffer__reset(&buffer), success(_) {
            TEST_ASSERT_EQUAL_INT(0, SmallBuffer__item_count(&buffer));
        }, error(code, message) { TEST_FAIL(); })
}

void test_ring_buffer_item_count_not_cycled()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (int i = 0; i < NUM_ITEMS / 2; i++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS / 2, count);
}

void test_ring_buffer_item_count_when_cycled()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (int i = 0; i < NUM_ITEMS / 2; i++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS, count);
}

void test_ring_buffer_item_count()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < NUM_ITEMS; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    size_t count = SmallBuffer__item_count(&buffer);

    TEST_ASSERT_EQUAL_INT(NUM_ITEMS, count);
}

void test_ring_buffer_free_space()
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 5; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
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
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < NUM_ITEMS / 2; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    match_result(ItemCount, SmallBuffer__drain(&buffer, drain, NUM_ITEMS), success(num) {
            TEST_ASSERT_EQUAL_INT(10, num);
            TEST_ASSERT_EQUAL_UINT8_ARRAY(expected, drain, NUM_ITEMS);
        }, error(code, message) { TEST_FAIL(); })
}

void test_ring_buffer_push_overflow(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 10; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    match_result(Pos, SmallBuffer__push(&buffer, 100), success(value) { TEST_FAIL(); }, error(code, message) { TEST_ASSERT_EQUAL_INT(RING_BUFFER_OVERFLOW, code); })
}

void test_ring_buffer_pop_underflow(void)
{
    SmallBuffer buffer = SmallBuffer__new();

    for (Char c = 0; c < 5; c++)
    {
        match_result(Pos, SmallBuffer__push(&buffer, c), success(_) {}, error(code, message) { TEST_FAIL(); })
    }

    for (Char c = 0; c < 5; c++)
    {
        match_result(Char, SmallBuffer__pop(&buffer), success(val) { TEST_ASSERT_EQUAL_INT(c, val); }, error(code, message) { TEST_FAIL(); })
    }

    match_result(Char, SmallBuffer__pop(&buffer), success(val) { TEST_FAIL(); }, error(code, message) { TEST_ASSERT_EQUAL_INT(RING_BUFFER_UNDERFLOW, code); })
}

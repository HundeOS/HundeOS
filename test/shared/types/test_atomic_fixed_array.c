#include <stdint.h>

#include "unity.h"
#include "../../../common/types/atomic_fixed_array.h"

AtomicFixedArrayDefine(uint64_t, 4, AtomicTestSlots);
AtomicFixedArrayDefine(uint32_t, 1, AtomicWordSlots);

void test_atomic_fixed_array_has_fixed_layout_and_zero_initialized_slots(void)
{
    AtomicTestSlots slots = AtomicTestSlots__new();
    TEST_ASSERT_EQUAL_UINT64(4 * sizeof(uint64_t), sizeof(slots));
    TEST_ASSERT_EQUAL_UINT64(_Alignof(uint64_t), _Alignof(AtomicTestSlots));
    TEST_ASSERT_EQUAL_UINT64(4, AtomicTestSlots__capacity(&slots));
    uint64_t visited = 0;
    for_each_atomic_fa_ptr(AtomicTestSlots, &slots, index, value, {
        TEST_ASSERT_EQUAL_UINT64(visited, index);
        TEST_ASSERT_EQUAL_UINT64(0, value);
        visited++;
    });
    TEST_ASSERT_EQUAL_UINT64(4, visited);
}

void test_atomic_fixed_array_rejects_out_of_bounds_access_without_mutation(void)
{
    AtomicTestSlots slots = AtomicTestSlots__new();
    match_result(AtomicTestSlots__set(&slots, 3, 99),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(
        AtomicTestSlots__set(&slots, 4, 17),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ARRAY_OUT_OF_BOUNDS, code); }
    );
    match_result(
        AtomicTestSlots__set(&slots, UINT64_MAX, 17),
        success_test_fail(),
        error(code, message) { TEST_ASSERT_EQUAL(ARRAY_OUT_OF_BOUNDS, code); }
    );
    match_optional(AtomicTestSlots__at(&slots, 4),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(AtomicTestSlots__at(&slots, UINT64_MAX),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        AtomicTestSlots__at(&slots, 3),
        some(value) { TEST_ASSERT_EQUAL_UINT64(99, value); },
        none_test_fail()
    );
}

void test_atomic_fixed_array_clearing_a_slot_does_not_shift_other_slots(void)
{
    AtomicTestSlots slots = AtomicTestSlots__new();
    match_result(AtomicTestSlots__set(&slots, 0, 11),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(AtomicTestSlots__set(&slots, 3, UINT64_MAX),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(AtomicTestSlots__set(&slots, 0, 0),
        success_do_nothing(),
        error_test_fail()
    );
    for_each_atomic_fa_ptr(AtomicTestSlots, &slots, index, value,
                           { TEST_ASSERT_EQUAL_UINT64(index == 3 ? UINT64_MAX : 0, value); });
}

void test_atomic_fixed_array_iteration_supports_break_continue_and_live_updates(void)
{
    AtomicTestSlots slots = AtomicTestSlots__new();
    uint64_t visited = 0;
    for_each_atomic_fa_ptr(AtomicTestSlots, &slots, index, value, {
        visited++;
        if (index == 0)
        {
            match_result(AtomicTestSlots__set(&slots, 2, 42),
                success_do_nothing(),
                error_test_fail()
            );
            continue;
        }
        if (index == 2)
        {
            TEST_ASSERT_EQUAL_UINT64(42, value);
            break;
        }
        TEST_ASSERT_EQUAL_UINT64(0, value);
    });
    TEST_ASSERT_EQUAL_UINT64(3, visited);
}

void test_atomic_fixed_array_supports_a_different_scalar_width_and_const_reads(void)
{
    AtomicWordSlots slots = AtomicWordSlots__new();
    match_result(AtomicWordSlots__set(&slots, 0, UINT32_MAX),
        success_do_nothing(),
        error_test_fail()
    );
    ConstAtomicWordSlotsPtr reader = &slots;
    TEST_ASSERT_EQUAL_UINT64(sizeof(uint32_t), sizeof(slots));
    TEST_ASSERT_EQUAL_UINT64(1, AtomicWordSlots__capacity(reader));
    match_optional(
        AtomicWordSlots__at(reader, 0),
        some(value) { TEST_ASSERT_EQUAL_UINT32(UINT32_MAX, value); },
        none_test_fail()
    );
}

void test_atomic_fixed_array_take_returns_and_clears_one_slot(void)
{
    AtomicTestSlots slots = AtomicTestSlots__new();
    match_result(AtomicTestSlots__set(&slots, 2, 73),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(
        AtomicTestSlots__take(&slots, 2),
        some(value) { TEST_ASSERT_EQUAL_UINT64(73, value); },
        none_test_fail()
    );
    match_optional(
        AtomicTestSlots__at(&slots, 2),
        some(value) { TEST_ASSERT_EQUAL_UINT64(0, value); },
        none_test_fail()
    );
    match_optional(AtomicTestSlots__take(&slots, 4),
        some_test_fail(),
        none_do_nothing()
    );
}

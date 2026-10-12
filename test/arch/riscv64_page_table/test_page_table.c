#include <stdint.h>

#include "unity.h"

#include "../../../arch/riscv64/page_table.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_riscv_table_entry_round_trips_physical_address(void)
{
    Address physical_address = 0x12345000ULL;
    RiscVPageTableEntry entry = RiscVPageTableEntry__table(physical_address);

    TEST_ASSERT_TRUE(RiscVPageTableEntry__is_table(entry));
    TEST_ASSERT_EQUAL_UINT64(physical_address, RiscVPageTableEntry__physical_address(entry));
}

void test_riscv_read_only_user_leaf_has_required_flags(void)
{
    RiscVPagePermissions permissions = RiscVPagePermissions__new(true, false, false, true);
    RiscVPageTableEntry entry = RiscVPageTableEntry__leaf(0x87654000ULL, &permissions, 0);

    TEST_ASSERT_EQUAL_UINT64(0x87654000ULL, RiscVPageTableEntry__physical_address(entry));
    TEST_ASSERT_BITS_HIGH(RISCV_PTE_VALID | RISCV_PTE_READ | RISCV_PTE_USER | RISCV_PTE_ACCESSED, entry);
    TEST_ASSERT_BITS_LOW(RISCV_PTE_WRITE | RISCV_PTE_EXECUTE | RISCV_PTE_DIRTY, entry);
    TEST_ASSERT_FALSE(RiscVPageTableEntry__is_table(entry));
}

void test_riscv_writable_leaf_is_readable_and_dirty(void)
{
    RiscVPagePermissions permissions = RiscVPagePermissions__new(false, true, false, true);
    RiscVPageTableEntry entry = RiscVPageTableEntry__leaf(0x80200000ULL, &permissions, 0);

    TEST_ASSERT_BITS_HIGH(RISCV_PTE_READ | RISCV_PTE_WRITE | RISCV_PTE_DIRTY, entry);
}

void test_riscv_executable_supervisor_leaf_is_not_user_accessible(void)
{
    RiscVPagePermissions permissions = RiscVPagePermissions__new(true, false, true, false);
    RiscVPageTableEntry entry = RiscVPageTableEntry__leaf(0x80400000ULL, &permissions, 0);

    TEST_ASSERT_BITS_HIGH(RISCV_PTE_READ | RISCV_PTE_EXECUTE, entry);
    TEST_ASSERT_BITS_LOW(RISCV_PTE_USER | RISCV_PTE_WRITE | RISCV_PTE_DIRTY, entry);
}

void test_riscv_leaf_preserves_memory_attributes_without_changing_physical_address(void)
{
    uint64_t memory_attributes = (1ULL << 60U) | (1ULL << 61U) | (1ULL << 62U);
    RiscVPagePermissions permissions = RiscVPagePermissions__new(true, true, false, true);
    RiscVPageTableEntry entry = RiscVPageTableEntry__leaf(0x87654000ULL, &permissions, memory_attributes);

    TEST_ASSERT_BITS_HIGH(memory_attributes, entry);
    TEST_ASSERT_EQUAL_UINT64(0x87654000ULL, RiscVPageTableEntry__physical_address(entry));
}

void test_riscv_sv39_indices_select_each_virtual_page_component(void)
{
    Address virtual_address = (0x155ULL << 30U) | (0x0aaULL << 21U) | (0x1feULL << 12U);

    TEST_ASSERT_EQUAL_UINT64(0x155ULL, RISCV_SV39_LEVEL_2_INDEX(virtual_address));
    TEST_ASSERT_EQUAL_UINT64(0x0aaULL, RISCV_SV39_LEVEL_1_INDEX(virtual_address));
    TEST_ASSERT_EQUAL_UINT64(0x1feULL, RISCV_SV39_LEVEL_0_INDEX(virtual_address));
}

void test_riscv_user_addresses_are_limited_to_canonical_lower_half(void)
{
    TEST_ASSERT_TRUE(RiscVVirtualAddress__is_user(RISCV_SV39_USER_ADDRESS_LIMIT - 1U));
    TEST_ASSERT_FALSE(RiscVVirtualAddress__is_user(RISCV_SV39_USER_ADDRESS_LIMIT));
    TEST_ASSERT_FALSE(RiscVVirtualAddress__is_user(1ULL << 39U));
}

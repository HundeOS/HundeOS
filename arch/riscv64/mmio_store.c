#include "mmio_store.h"

#define RISCV_OPCODE_STORE 0x23U
#define RISCV_COMPRESSED_QUADRANT_ZERO 0x0U
#define RISCV_COMPRESSED_QUADRANT_TWO 0x2U
#define RISCV_COMPRESSED_STORE_WORD 0x6U
#define RISCV_COMPRESSED_STORE_DOUBLE 0x7U
#define RISCV_FENCE_IORW_IORW 0x0ff0000fU

static Address RiscVMMIOStore__address(ProcessContextPtr context, uint32_t base_register, int64_t offset)
{
    return context->x[base_register] + (uint64_t)offset;
}

static Optional(RiscVMMIOStore) RiscVMMIOStore__standard(uint32_t instruction, ProcessContextPtr context)
{
    if ((instruction & 0x7fU) != RISCV_OPCODE_STORE)
    {
        return None(RiscVMMIOStore);
    }

    uint32_t width_code = (instruction >> 12U) & 0x7U;
    if (width_code > 3U)
    {
        return None(RiscVMMIOStore);
    }

    uint32_t source_register = (instruction >> 20U) & 0x1fU;
    uint32_t base_register = (instruction >> 15U) & 0x1fU;
    uint32_t immediate = ((instruction >> 25U) << 5U) | ((instruction >> 7U) & 0x1fU);
    int64_t offset = (int64_t)(immediate | ((immediate & 0x800U) != 0 ? 0xfffffffffffff000ULL : 0ULL));
    RiscVMMIOStore store =
        RiscVMMIOStore__new(RiscVMMIOStore__address(context, base_register, offset), context->x[source_register],
                            1ULL << width_code, sizeof(uint32_t));
    return Some(RiscVMMIOStore, store);
}

static Optional(RiscVMMIOStore) RiscVMMIOStore__compressed_register(uint16_t instruction,
                                                                    ProcessContextPtr context)
{
    uint32_t operation = instruction >> 13U;
    uint32_t source_register = 8U + ((instruction >> 2U) & 0x7U);
    uint32_t base_register = 8U + ((instruction >> 7U) & 0x7U);
    size_t width;
    uint32_t offset;

    if (operation == RISCV_COMPRESSED_STORE_WORD)
    {
        width = sizeof(uint32_t);
        offset = ((instruction >> 10U) & 0x7U) << 3U;
        offset |= ((instruction >> 6U) & 0x1U) << 2U;
        offset |= ((instruction >> 5U) & 0x1U) << 6U;
    }
    else if (operation == RISCV_COMPRESSED_STORE_DOUBLE)
    {
        width = sizeof(uint64_t);
        offset = ((instruction >> 10U) & 0x7U) << 3U;
        offset |= ((instruction >> 5U) & 0x3U) << 6U;
    }
    else
    {
        return None(RiscVMMIOStore);
    }

    RiscVMMIOStore store =
        RiscVMMIOStore__new(RiscVMMIOStore__address(context, base_register, offset), context->x[source_register],
                            width, sizeof(uint16_t));
    return Some(RiscVMMIOStore, store);
}

static Optional(RiscVMMIOStore) RiscVMMIOStore__compressed_stack(uint16_t instruction, ProcessContextPtr context)
{
    uint32_t operation = instruction >> 13U;
    uint32_t source_register = (instruction >> 2U) & 0x1fU;
    size_t width;
    uint32_t offset;

    if (operation == RISCV_COMPRESSED_STORE_WORD)
    {
        width = sizeof(uint32_t);
        offset = ((instruction >> 9U) & 0xfU) << 2U;
        offset |= ((instruction >> 7U) & 0x3U) << 6U;
    }
    else if (operation == RISCV_COMPRESSED_STORE_DOUBLE)
    {
        width = sizeof(uint64_t);
        offset = ((instruction >> 10U) & 0x7U) << 3U;
        offset |= ((instruction >> 7U) & 0x7U) << 6U;
    }
    else
    {
        return None(RiscVMMIOStore);
    }

    RiscVMMIOStore store = RiscVMMIOStore__new(RiscVMMIOStore__address(context, 2U, offset),
                                               context->x[source_register], width, sizeof(uint16_t));
    return Some(RiscVMMIOStore, store);
}

Optional(RiscVMMIOStore) RiscVMMIOStore__decode(uint32_t instruction, ProcessContextPtr context)
{
    if ((instruction & 0x3U) == 0x3U)
    {
        return RiscVMMIOStore__standard(instruction, context);
    }

    uint16_t compressed = (uint16_t)instruction;
    uint32_t quadrant = compressed & 0x3U;
    if (quadrant == RISCV_COMPRESSED_QUADRANT_ZERO)
    {
        return RiscVMMIOStore__compressed_register(compressed, context);
    }
    if (quadrant == RISCV_COMPRESSED_QUADRANT_TWO)
    {
        return RiscVMMIOStore__compressed_stack(compressed, context);
    }
    return None(RiscVMMIOStore);
}

void RiscVMMIOStore__perform(ConstRiscVMMIOStorePtr self, Address access_address, ProcessContextPtr context)
{
    if (self->width == sizeof(uint8_t))
    {
        *(volatile uint8_t *)access_address = (uint8_t)self->value;
    }
    else if (self->width == sizeof(uint16_t))
    {
        *(volatile uint16_t *)access_address = (uint16_t)self->value;
    }
    else if (self->width == sizeof(uint32_t))
    {
        *(volatile uint32_t *)access_address = (uint32_t)self->value;
    }
    else
    {
        *(volatile uint64_t *)access_address = self->value;
    }
    context->sepc += self->instruction_size;
}

bool RiscVMMIOStore__skip_following_fence(uint32_t instruction, ProcessContextPtr context)
{
    if (instruction != RISCV_FENCE_IORW_IORW)
    {
        return false;
    }
    context->sepc += sizeof(uint32_t);
    return true;
}

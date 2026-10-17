#include "mmio_load.h"

#define RISCV_OPCODE_LOAD 0x03U
#define RISCV_COMPRESSED_QUADRANT_ZERO 0x0U
#define RISCV_COMPRESSED_QUADRANT_TWO 0x2U
#define RISCV_COMPRESSED_LOAD_WORD 0x2U
#define RISCV_COMPRESSED_LOAD_DOUBLE 0x3U

static Address RiscVMMIOLoad__address(ProcessContextPtr context, uint32_t base_register, int64_t offset)
{
    return context->x[base_register] + (uint64_t)offset;
}

static Optional(RiscVMMIOLoad) RiscVMMIOLoad__standard(uint32_t instruction, ProcessContextPtr context)
{
    if ((instruction & 0x7fU) != RISCV_OPCODE_LOAD)
    {
        return None(RiscVMMIOLoad);
    }

    uint32_t width_code = (instruction >> 12U) & 0x7U;
    size_t width;
    bool sign_extend;
    if (width_code <= 3U)
    {
        width = 1ULL << width_code;
        sign_extend = width_code != 3U;
    }
    else if (width_code >= 4U && width_code <= 6U)
    {
        width = 1ULL << (width_code - 4U);
        sign_extend = false;
    }
    else
    {
        return None(RiscVMMIOLoad);
    }

    uint32_t destination_register = (instruction >> 7U) & 0x1fU;
    uint32_t base_register = (instruction >> 15U) & 0x1fU;
    uint32_t immediate = instruction >> 20U;
    int64_t offset = (int64_t)(immediate | ((immediate & 0x800U) != 0 ? 0xfffffffffffff000ULL : 0ULL));
    RiscVMMIOLoad load = RiscVMMIOLoad__new(RiscVMMIOLoad__address(context, base_register, offset),
                                            destination_register, width, sign_extend, sizeof(uint32_t));
    return Some(RiscVMMIOLoad, load);
}

static Optional(RiscVMMIOLoad) RiscVMMIOLoad__compressed_register(uint16_t instruction, ProcessContextPtr context)
{
    uint32_t operation = instruction >> 13U;
    uint32_t destination_register = 8U + ((instruction >> 2U) & 0x7U);
    uint32_t base_register = 8U + ((instruction >> 7U) & 0x7U);
    size_t width;
    uint32_t offset;

    if (operation == RISCV_COMPRESSED_LOAD_WORD)
    {
        width = sizeof(uint32_t);
        offset = ((instruction >> 10U) & 0x7U) << 3U;
        offset |= ((instruction >> 6U) & 0x1U) << 2U;
        offset |= ((instruction >> 5U) & 0x1U) << 6U;
    }
    else if (operation == RISCV_COMPRESSED_LOAD_DOUBLE)
    {
        width = sizeof(uint64_t);
        offset = ((instruction >> 10U) & 0x7U) << 3U;
        offset |= ((instruction >> 5U) & 0x3U) << 6U;
    }
    else
    {
        return None(RiscVMMIOLoad);
    }

    RiscVMMIOLoad load = RiscVMMIOLoad__new(RiscVMMIOLoad__address(context, base_register, offset),
                                            destination_register, width, width == sizeof(uint32_t), sizeof(uint16_t));
    return Some(RiscVMMIOLoad, load);
}

static Optional(RiscVMMIOLoad) RiscVMMIOLoad__compressed_stack(uint16_t instruction, ProcessContextPtr context)
{
    uint32_t operation = instruction >> 13U;
    uint32_t destination_register = (instruction >> 7U) & 0x1fU;
    size_t width;
    uint32_t offset;

    if (destination_register == 0U)
    {
        return None(RiscVMMIOLoad);
    }

    if (operation == RISCV_COMPRESSED_LOAD_WORD)
    {
        width = sizeof(uint32_t);
        offset = ((instruction >> 4U) & 0x7U) << 2U;
        offset |= ((instruction >> 12U) & 0x1U) << 5U;
        offset |= ((instruction >> 2U) & 0x3U) << 6U;
    }
    else if (operation == RISCV_COMPRESSED_LOAD_DOUBLE)
    {
        width = sizeof(uint64_t);
        offset = ((instruction >> 5U) & 0x3U) << 3U;
        offset |= ((instruction >> 12U) & 0x1U) << 5U;
        offset |= ((instruction >> 2U) & 0x7U) << 6U;
    }
    else
    {
        return None(RiscVMMIOLoad);
    }

    RiscVMMIOLoad load = RiscVMMIOLoad__new(RiscVMMIOLoad__address(context, 2U, offset), destination_register, width,
                                            width == sizeof(uint32_t), sizeof(uint16_t));
    return Some(RiscVMMIOLoad, load);
}

Optional(RiscVMMIOLoad) RiscVMMIOLoad__decode(uint32_t instruction, ProcessContextPtr context)
{
    if ((instruction & 0x3U) == 0x3U)
    {
        return RiscVMMIOLoad__standard(instruction, context);
    }

    uint16_t compressed = (uint16_t)instruction;
    uint32_t quadrant = compressed & 0x3U;
    if (quadrant == RISCV_COMPRESSED_QUADRANT_ZERO)
    {
        return RiscVMMIOLoad__compressed_register(compressed, context);
    }
    if (quadrant == RISCV_COMPRESSED_QUADRANT_TWO)
    {
        return RiscVMMIOLoad__compressed_stack(compressed, context);
    }
    return None(RiscVMMIOLoad);
}

void RiscVMMIOLoad__perform(ConstRiscVMMIOLoadPtr self, Address access_address, ProcessContextPtr context)
{
    uint64_t value;
    if (self->width == sizeof(uint8_t))
    {
        uint8_t loaded = *(volatile uint8_t *)access_address;
        value = self->sign_extend ? (uint64_t)(int64_t)(int8_t)loaded : loaded;
    }
    else if (self->width == sizeof(uint16_t))
    {
        uint16_t loaded = *(volatile uint16_t *)access_address;
        value = self->sign_extend ? (uint64_t)(int64_t)(int16_t)loaded : loaded;
    }
    else if (self->width == sizeof(uint32_t))
    {
        uint32_t loaded = *(volatile uint32_t *)access_address;
        value = self->sign_extend ? (uint64_t)(int64_t)(int32_t)loaded : loaded;
    }
    else
    {
        value = *(volatile uint64_t *)access_address;
    }
    if (self->destination_register != 0U)
    {
        context->x[self->destination_register] = value;
    }
    context->sepc += self->instruction_size;
}

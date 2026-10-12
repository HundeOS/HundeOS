#ifndef __RISCV64_MMIO_LOAD_H
#define __RISCV64_MMIO_LOAD_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../kernel/arch/process_context.h"
#include "../../kernel/types/optional.h"
#include "process.h"

typedef struct RiscVMMIOLoad
{
    Address address;
    uint32_t destination_register;
    size_t width;
    bool sign_extend;
    size_t instruction_size;
} RiscVMMIOLoad;

typedef RiscVMMIOLoad * RiscVMMIOLoadPtr;
typedef const RiscVMMIOLoad * ConstRiscVMMIOLoadPtr;

OptionalDef(RiscVMMIOLoad);

static inline RiscVMMIOLoad RiscVMMIOLoad__new(Address address, uint32_t destination_register, size_t width,
                                                bool sign_extend, size_t instruction_size)
{
    return (RiscVMMIOLoad){
        .address = address,
        .destination_register = destination_register,
        .width = width,
        .sign_extend = sign_extend,
        .instruction_size = instruction_size,
    };
}

Optional(RiscVMMIOLoad) RiscVMMIOLoad__decode(uint32_t instruction, ProcessContextPtr context);
void RiscVMMIOLoad__perform(ConstRiscVMMIOLoadPtr self, Address access_address, ProcessContextPtr context);

#endif

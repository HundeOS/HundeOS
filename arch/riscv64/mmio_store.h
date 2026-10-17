#ifndef __RISCV64_MMIO_STORE_H
#define __RISCV64_MMIO_STORE_H

#include <stddef.h>
#include <stdint.h>

#include "../../kernel/arch/process_context.h"
#include "../../kernel/types/optional.h"
#include "process.h"

typedef struct RiscVMMIOStore
{
    Address address;
    uint64_t value;
    size_t width;
    size_t instruction_size;
} RiscVMMIOStore;

typedef RiscVMMIOStore * RiscVMMIOStorePtr;
typedef const RiscVMMIOStore * ConstRiscVMMIOStorePtr;

OptionalDef(RiscVMMIOStore);

static inline RiscVMMIOStore RiscVMMIOStore__new(Address address, uint64_t value, size_t width, size_t instruction_size)
{
    return (RiscVMMIOStore){
        .address = address,
        .value = value,
        .width = width,
        .instruction_size = instruction_size,
    };
}

Optional(RiscVMMIOStore) RiscVMMIOStore__decode(uint32_t instruction, ProcessContextPtr context);
void RiscVMMIOStore__perform(ConstRiscVMMIOStorePtr self, Address access_address, ProcessContextPtr context);
bool RiscVMMIOStore__skip_following_fence(uint32_t instruction, ProcessContextPtr context);

#endif

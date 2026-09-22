#include "../../kernel/arch/cache.h"

void Arch__sync_instruction_memory(Address address, size_t size)
{
    __asm__ volatile("fence rw, rw\n"
                     "fence.i" ::
                         : "memory");
}

#ifndef __KERNEL_ARCH_CACHE_H
#define __KERNEL_ARCH_CACHE_H

#include <stddef.h>

#include "../address.h"

void Arch__sync_instruction_memory(Address address, size_t size);

#endif

#ifndef __TEST_KERNEL_STUBS_KALLOC_H__
#define __TEST_KERNEL_STUBS_KALLOC_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../../kernel/address.h"

void KAllocStub__reset_virtual_translation(void);
void KAllocStub__set_virtual_translation(Address virtual_start, size_t size, Address physical_start);
void KAllocStub__set_alloc_fill(bool enabled, uint8_t value);

#endif

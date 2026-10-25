#ifndef __TEST_KERNEL_STUBS_KALLOC_H__
#define __TEST_KERNEL_STUBS_KALLOC_H__

#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

#include "../../../kernel/address.h"
#include "../../../kernel/types/optional.h"

typedef void (*KAllocObserver)(void);
OptionalDef(KAllocObserver);

void KAllocStub__observe_user_allocations(Optional(KAllocObserver) observer);

void KAllocStub__reset_virtual_translation(void);
void KAllocStub__set_virtual_translation(Address virtual_start, size_t size, Address physical_start);
void KAllocStub__set_alloc_fill(bool enabled, uint8_t value);
void KAllocStub__set_user_alloc_failure(bool enabled);
void KAllocStub__set_free_failure(bool enabled);
void KAllocStub__reset_allocation_tracking(void);
size_t KAllocStub__alloc_count(void);
size_t KAllocStub__free_count(void);
bool KAllocStub__is_allocated(Address address);
size_t KAllocStub__freed_allocation_count_for_size(size_t size);
bool KAllocStub__memcopy_overflow_occurred(void);

#endif

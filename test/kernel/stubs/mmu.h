#ifndef __KERNEL_STUBS_MMU_H_
#define __KERNEL_STUBS_MMU_H_

#include "../../../kernel/address.h"

void reset_mmu_state();
KernelAddress MMUStub__active_table(void);
uint64_t MMUStub__dereference_count(void);
bool MMUStub__dereferenced_before_free(void);

#endif

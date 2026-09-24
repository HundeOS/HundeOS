#ifndef __KERNEL_ARCH_PROCESS_H
#define __KERNEL_ARCH_PROCESS_H

#include <stdint.h>
#include "process_context.h"
#include "../../common_includes/syscalls/types.h"

typedef uint64_t ProcessID;

typedef struct Process Process;
typedef Process * ProcessPtr;

void Process__switch_address_space(ProcessPtr self, ProcessContextPtr context, KernelAddress page_table);
void Process__switch_context(ProcessPtr self, ProcessContextPtr context, KernelAddress page_table);
uint16_t Arch__elf_machine(void);
void Process__set_syscall_result(ProcessPtr self, SystemCallResult result);
void Process__set_startup_stdio(ProcessPtr self, int64_t input, int64_t output, int64_t error);

#endif

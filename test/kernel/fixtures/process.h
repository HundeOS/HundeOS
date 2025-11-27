#ifndef __TEST_KERNEL_FIXTURES_PROCESS_H_
#define __TEST_KERNEL_FIXTURES_PROCESS_H_

#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/bin/embedded_binaries.h"

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state);
ProcessPtr dummy_process_with_parent(Tick last_tick, CPUID cpu, ProcessState state, ProcessPtr parent);

#endif
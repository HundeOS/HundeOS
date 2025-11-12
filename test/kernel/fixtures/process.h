#ifndef __TEST_KERNEL_FIXTURES_PROCESS_H_
#define __TEST_KERNEL_FIXTURES_PROCESS_H_

#include "unity.h"
#include "../../../kernel/process.h"
#include "../../../kernel/bin/embedded_binaries.h"

ProcessPtr dummy_process(Tick last_tick, CPUID cpu, ProcessState state);

#endif
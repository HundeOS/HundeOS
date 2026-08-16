#ifndef __AARCH64_BOOT_CPU_H_
#define __AARCH64_BOOT_CPU_H_

#include <stdint.h>
#include "boot.h"
#include "../cpu_common.h"

__BOOT__(void) boot_secondary_cpus(uint64_t cpu_count);

#endif

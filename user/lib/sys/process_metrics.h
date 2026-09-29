#ifndef HUND_USER_PROCESS_METRICS_H
#define HUND_USER_PROCESS_METRICS_H

#include "process/process_registry.h"

ResultDef(ProcessInfo);

Result(ProcessInfo) ProcessMetrics__info(uint64_t pid);
Result(ProcessRecord)
    ProcessMetrics__snapshot_after(ConstProcessRegistryPtr registry, uint64_t pid, uint64_t sampled_after_usec);

#endif

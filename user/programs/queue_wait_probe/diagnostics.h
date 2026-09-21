#ifndef WAIT_PROBE_DIAGNOSTICS_H
#define WAIT_PROBE_DIAGNOSTICS_H

#include "process/process_registry.h"

Result(ProcessRecord) WaitProbe__snapshot(ConstProcessRegistryPtr registry, uint64_t pid, uint64_t sampled_after);

#endif

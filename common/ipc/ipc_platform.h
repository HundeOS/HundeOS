#ifndef HUND_IPC_PLATFORM_H
#define HUND_IPC_PLATFORM_H

#include <stdint.h>
#include "hardware/boot_info.h"

void IpcPlatform__configure_wait(BootWaitMode mode);
void IpcPlatform__signal_event(void);
// May return spuriously; callers must recheck authoritative shared state.
void IpcPlatform__wait_event(void);
uint64_t IpcPlatform__monotonic_usec(void);
void IpcPlatform__relax(void);

#endif

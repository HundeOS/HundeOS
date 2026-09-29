#ifndef HUND_IPC_PLATFORM_H
#define HUND_IPC_PLATFORM_H

#include <stdint.h>
#include "hardware/boot_info.h"
#include "doorbell.h"

void IpcPlatform__configure_wait(BootWaitMode mode);
void IpcPlatform__configure_timer_frequency(uint64_t frequency);
IpcDoorbellPtr IpcPlatform__local_doorbell(void);
void IpcPlatform__signal_event(void);
IpcEventGeneration IpcPlatform__prepare_wait(ConstIpcEventPtr event);
// May return spuriously; callers must recheck both the event and authoritative shared state.
void IpcPlatform__wait_event(ConstIpcEventPtr event, IpcEventGeneration observed);
uint64_t IpcPlatform__monotonic_usec(void);
void IpcPlatform__relax(void);

#endif

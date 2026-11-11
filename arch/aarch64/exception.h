#ifndef __AARCH64_EXCEPTION_H_
#define __AARCH64_EXCEPTION_H_

#include "../../kernel/arch/exception.h"
#include "../../kernel/arch/thread_context.h"

uint64_t Exception__handle_sync_current_sp0(ThreadContextPtr context);
void Exception__handle_irq_current_sp0(ThreadContextPtr context);
void Exception__handle_fiq_current_sp0(ThreadContextPtr context);
void Exception__handle_serror_current_sp0(ThreadContextPtr context);

uint64_t Exception__handle_sync_current_spx(ThreadContextPtr context);
void Exception__handle_irq_current_spx(ThreadContextPtr context);
void Exception__handle_fiq_current_spx(ThreadContextPtr context);
void Exception__handle_serror_current_spx(ThreadContextPtr context);

uint64_t Exception__handle_sync_lower_aarch64(ThreadContextPtr context);
void Exception__handle_irq_lower_aarch64(ThreadContextPtr context);
void Exception__handle_fiq_lower_aarch64(ThreadContextPtr context);
void Exception__handle_serror_lower_aarch64(ThreadContextPtr context);

void Exception__handle_sync_lower_aarch32(ThreadContextPtr context);
void Exception__handle_irq_lower_aarch32(ThreadContextPtr context);
void Exception__handle_fiq_lower_aarch32(ThreadContextPtr context);
void Exception__handle_serror_lower_aarch32(ThreadContextPtr context);

#endif /* _EXCEPTION_H_ */

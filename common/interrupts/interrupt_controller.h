#ifndef HUND_INTERRUPT_CONTROLLER_H
#define HUND_INTERRUPT_CONTROLLER_H

#include "interrupt_types.h"
#include "../ipc/kernel_notification_queue.h"

void InterruptController__unmask(Address distributor, IRQNumber irq);

#endif

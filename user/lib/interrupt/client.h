#ifndef HUND_INTERRUPT_CLIENT_H
#define HUND_INTERRUPT_CLIENT_H
#include "ipc/interrupt_protocol.h"
#include "sys/service.h"
#include "channel/channel.h"
ServiceCode InterruptClient__subscribe(IRQNumber irq);
ServiceCode InterruptClient__poll(ServiceEventPtr event);
ServiceCode InterruptClient__wait(ServiceEventPtr event);
ServiceCode InterruptClient__acknowledge(IRQNumber irq);
#endif

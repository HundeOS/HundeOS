#ifndef HUND_VIRTIO_BLK_CHANNELIO_H
#define HUND_VIRTIO_BLK_CHANNELIO_H

#include "ipc/interrupt_protocol.h"
#include "sys/service.h"

ServiceCode VirtioBlkChannelIO__subscribe(IRQNumber irq);
ServiceCode VirtioBlkChannelIO__poll(ServiceEventPtr event);
ServiceCode VirtioBlkChannelIO__wait(ServiceEventPtr event);
ServiceCode VirtioBlkChannelIO__acknowledge(IRQNumber irq);

#endif

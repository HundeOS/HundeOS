#ifndef HUND_VIRTIO_BLK_RUNTIME_H
#define HUND_VIRTIO_BLK_RUNTIME_H

#include "device.h"
#include "fiber/scheduler.h"

typedef ServiceCode (*VirtioBlkInterruptSubscribe)(Anything context, IRQNumber irq);
typedef ServiceCode (*VirtioBlkInterruptRearm)(Anything context, IRQNumber irq);

typedef struct VirtioBlkInterruptBackend
{
    Anything context;
    VirtioBlkInterruptSubscribe subscribe;
    VirtioBlkInterruptRearm rearm;
} VirtioBlkInterruptBackend;

typedef const VirtioBlkInterruptBackend * ConstVirtioBlkInterruptBackendPtr;

struct VirtioBlkRuntime
{
    VirtioBlkDevice device;
    FiberScheduler scheduler;
    BlockServer server;
    ChannelSignalState work_state;
    ChannelSignal work_available;
    VirtioBlkInterruptBackend interrupts;
};

typedef VirtioBlkRuntime * VirtioBlkRuntimePtr;
OptionalDef(VirtioBlkRuntimePtr);

VirtioBlkInterruptBackend VirtioBlkInterruptBackend__new(Anything context, VirtioBlkInterruptSubscribe subscribe,
                                                         VirtioBlkInterruptRearm rearm);
ServiceCode VirtioBlkRuntime__start(VirtioBlkRuntimePtr self, uint64_t slot, ConstBStringPtr path,
                                    ConstVirtioBlkInterruptBackendPtr interrupts);
bool VirtioBlkRuntime__handle_irq(VirtioBlkRuntimePtr self, ConstKernelNotificationPtr event,
                                  uint64_t manager_received_at_usec);
Result(FiberCode) VirtioBlkRuntime__step(VirtioBlkRuntimePtr self);

#endif

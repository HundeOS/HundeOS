#ifndef HUND_VIRTIO_BLK_DEVICE_H
#define HUND_VIRTIO_BLK_DEVICE_H
#include "sys/service.h"
#include "channel/block_server.h"
#include "interrupt/client.h"

typedef struct VirtioBlkRuntime VirtioBlkRuntime;
typedef VirtioBlkRuntime * VirtioBlkRuntimePtr;

typedef struct VirtioBlkDevice
{
    ServiceMappedMemoryPtr mapping;
    void * mmio;
    uint64_t slot;
    uint64_t irq_id;
    uint64_t block_count;
    BString path;
    uint16_t last_used_idx;
    bool io_failed;
    Optional(InterruptDeliveryTiming) irq_timing;
    uint64_t fiber_resumed_at_usec;
    VirtioBlkRuntimePtr runtime;
} VirtioBlkDevice, *VirtioBlkDevicePtr;

typedef const VirtioBlkDevice * ConstVirtioBlkDevicePtr;

ResultDef(VirtioBlkDevice);
uint32_t VirtioBlkMmio__read32(void * base, uint64_t offset);
void VirtioBlkMmio__write32(void * base, uint64_t offset, uint32_t value);
void VirtioBlk__ack_interrupt(void * mmio);
Result(VirtioBlkDevice) VirtioBlk__start(uint64_t slot, ConstBStringPtr path);
ChannelCode VirtioBlk__listen(VirtioBlkDevicePtr self, BlockServerPtr server);
ServiceCode VirtioBlkRuntime__await_irq(VirtioBlkDevicePtr device, ServiceEventPtr event);
ServiceCode VirtioBlkRuntime__rearm_irq(VirtioBlkDevicePtr device);
#endif

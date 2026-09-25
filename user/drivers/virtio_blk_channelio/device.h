#ifndef HUND_VIRTIO_BLK_DEVICE_H
#define HUND_VIRTIO_BLK_DEVICE_H
#include "sys/service.h"
#include "channel/block_server.h"

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
} VirtioBlkDevice, *VirtioBlkDevicePtr;

ResultDef(VirtioBlkDevice);
uint32_t VirtioBlkMmio__read32(void * base, uint64_t offset);
void VirtioBlkMmio__write32(void * base, uint64_t offset, uint32_t value);
void VirtioBlk__ack_interrupt(void * mmio);
int VirtioBlk__run(ConstBStringPtr slot_argument, ConstBStringPtr path);
ServiceCode VirtioBlkRuntime__await_irq(ServiceEventPtr event);
int VirtioBlkRuntime__run(VirtioBlkDevicePtr device, BlockServerPtr server);
#endif

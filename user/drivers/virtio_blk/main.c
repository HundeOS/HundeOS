#define DRIVER_LOG_TAG "VIRTIO-BLK"

#include "../../sys/debug.h"
#include "../../sys/service.h"
#include "../../sys/mmio.h"

#ifndef VIRTIO_BLK_ENTRYPOINT
#define VIRTIO_BLK_ENTRYPOINT main
#endif

#ifndef VIRTIO_BLK_TEST_DEVICE_READY
#define VIRTIO_BLK_TEST_DEVICE_READY(device)
#endif

#define VIRTIO_MMIO_FIRST_PHYS_BASE 0x0a000000ULL
#define VIRTIO_MMIO_SLOT_SIZE       0x200ULL
#define VIRTIO_MMIO_SLOT_COUNT      32ULL
#define VIRTIO_MMIO_REGION_SIZE     (VIRTIO_MMIO_SLOT_SIZE * VIRTIO_MMIO_SLOT_COUNT)
#define VIRTIO_MMIO_FIRST_IRQ_ID    48ULL

#define VIRTIO_MMIO_MAGIC          0x000
#define VIRTIO_MMIO_VERSION        0x004
#define VIRTIO_MMIO_DEVICE_ID      0x008
#define VIRTIO_MMIO_VENDOR_ID      0x00c
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL      0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX  0x034
#define VIRTIO_MMIO_QUEUE_NUM      0x038
#define VIRTIO_MMIO_QUEUE_ALIGN    0x03c
#define VIRTIO_MMIO_QUEUE_PFN      0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY   0x050
#define VIRTIO_MMIO_INTERRUPT_STAT 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK  0x064
#define VIRTIO_MMIO_STATUS         0x070

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK  2U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER      2U
#define VIRTIO_STATUS_DRIVER_OK   4U

#define VIRTIO_BLK_QUEUE_INDEX 0U
#define VIRTIO_BLK_QUEUE_SIZE  8U
#define VIRTIO_BLK_QUEUE_ALIGN 4096U
#define VIRTIO_BLK_QUEUE_BYTES 8192U
#define VIRTIO_BLK_DESCS_PER_REQUEST 3U
#define VIRTIO_BLK_MAX_IN_FLIGHT (VIRTIO_BLK_QUEUE_SIZE / VIRTIO_BLK_DESCS_PER_REQUEST)
#define VIRTIO_BLK_PENDING_QUEUE_SIZE 16U

#define VIRTQ_DESC_F_NEXT  1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN  0U
#define VIRTIO_BLK_T_OUT 1U

#define VIRTIO_BLK_S_OK    0U
#define VIRTIO_BLK_S_IOERR 1U

typedef struct VirtqDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
}
VirtqDesc;

typedef struct VirtqAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_BLK_QUEUE_SIZE];
}
VirtqAvail;

typedef struct VirtqUsedElem
{
    uint32_t id;
    uint32_t len;
}
VirtqUsedElem;

typedef struct VirtqUsed
{
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[VIRTIO_BLK_QUEUE_SIZE];
}
VirtqUsed;

typedef struct VirtioBlkOuthdr
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
}
VirtioBlkOuthdr;

typedef struct VirtioBlkPendingRequest
{
    bool active;
    bool completed;
    uint64_t request_id;
    uint64_t submit_sequence;
    uint64_t block_count;
    ServiceCode service_status;
    uint64_t completed_blocks;
    VirtioBlkOuthdr header;
    uint8_t status;
}
VirtioBlkPendingRequest;

typedef VirtioBlkPendingRequest * VirtioBlkPendingRequestPtr;

typedef struct VirtioBlkDevice
{
    ServiceMappedMemoryPtr mapping;
    void * mmio;
    uint64_t slot;
    uint64_t irq_id;
    uint16_t last_used_idx;
    uint64_t next_submit_sequence;
    uint64_t next_delivery_sequence;
    VirtioBlkPendingRequest in_flight[VIRTIO_BLK_MAX_IN_FLIGHT];
    uint64_t pending_ids[VIRTIO_BLK_PENDING_QUEUE_SIZE];
    uint64_t pending_read_index;
    uint64_t pending_write_index;
    uint64_t pending_count;
} VirtioBlkDevice, * VirtioBlkDevicePtr;

static uint8_t virtio_queue_area[VIRTIO_BLK_QUEUE_BYTES] __attribute__((aligned(VIRTIO_BLK_QUEUE_ALIGN)));

static void zero_bytes(void * target, uint64_t size)
{
    uint8_t * bytes = (uint8_t *) target;
    for (uint64_t i = 0; i < size; i++)
    {
        bytes[i] = 0;
    }
}

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static VirtqDesc * VirtioBlk__descs()
{
    return (VirtqDesc *) virtio_queue_area;
}

static VirtqAvail * VirtioBlk__avail()
{
    return (VirtqAvail *) (virtio_queue_area + sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE);
}

static VirtqUsed * VirtioBlk__used()
{
    uint64_t used_offset = align_up(
        sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE + sizeof(VirtqAvail),
        VIRTIO_BLK_QUEUE_ALIGN
    );
    return (VirtqUsed *) (virtio_queue_area + used_offset);
}

#if !defined(VIRTIO_READ32) || !defined(VIRTIO_WRITE32)
static volatile uint32_t * reg32(void * base, uint64_t offset)
{
    return (volatile uint32_t *) ((uint64_t) base + offset);
}
#endif

#ifndef VIRTIO_READ32
#define VIRTIO_READ32(base, offset) (*reg32((base), (offset)))
#endif

#ifndef VIRTIO_WRITE32
#define VIRTIO_WRITE32(base, offset, value) (*reg32((base), (offset)) = (uint32_t) (value))
#endif

static bool VirtioBlk__probe(void * mmio, uint64_t slot, uint64_t physical_address)
{
    uint32_t magic = VIRTIO_READ32(mmio, VIRTIO_MMIO_MAGIC);
    uint32_t version = VIRTIO_READ32(mmio, VIRTIO_MMIO_VERSION);
    uint32_t device_id = VIRTIO_READ32(mmio, VIRTIO_MMIO_DEVICE_ID);
    uint32_t vendor_id = VIRTIO_READ32(mmio, VIRTIO_MMIO_VENDOR_ID);

    DRIVER_DEBUG(
        "virtio_blk: probe slot=%i base=0x%x magic=0x%x version=%i device_id=%i vendor=0x%x\n",
        (int) slot,
        (unsigned int) physical_address,
        magic,
        (int) version,
        (int) device_id,
        vendor_id
    );

    return magic == VIRTIO_MMIO_MAGIC_VALUE
        && version >= 1
        && device_id == VIRTIO_DEVICE_ID_BLOCK;
}

static bool VirtioBlk__find_device(VirtioBlkDevicePtr self)
{
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(VIRTIO_MMIO_FIRST_PHYS_BASE, VIRTIO_MMIO_REGION_SIZE, &mapping)))
    {
        DRIVER_DEBUG("virtio_blk: mmio region map failed\n");
        return false;
    }

    for (uint64_t slot = 0; slot < VIRTIO_MMIO_SLOT_COUNT; slot++)
    {
        uint64_t physical_address = VIRTIO_MMIO_FIRST_PHYS_BASE + (slot * VIRTIO_MMIO_SLOT_SIZE);

        void * mmio = driver_ptr_offset(mapping, slot * VIRTIO_MMIO_SLOT_SIZE);
        if (VirtioBlk__probe(mmio, slot, physical_address))
        {
            self->mapping = mapping;
            self->mmio = mmio;
            self->slot = slot;
            self->irq_id = VIRTIO_MMIO_FIRST_IRQ_ID + slot;
            self->last_used_idx = 0;
            self->next_submit_sequence = 0;
            self->next_delivery_sequence = 0;
            return true;
        }
    }

    return false;
}

static void VirtioBlk__acknowledge_driver(void * mmio)
{
    VIRTIO_WRITE32(mmio, VIRTIO_MMIO_STATUS, 0);
    VIRTIO_WRITE32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    VIRTIO_WRITE32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
}

static bool VirtioBlk__setup_queue(VirtioBlkDevicePtr self)
{
    uint64_t queue_device_address = 0;
    if (!service_ok(service_device_address(virtio_queue_area, &queue_device_address)))
    {
        DRIVER_DEBUG("virtio_blk: queue address translation failed\n");
        return false;
    }

    if ((queue_device_address & (VIRTIO_BLK_QUEUE_ALIGN - 1)) != 0)
    {
        DRIVER_DEBUG("virtio_blk: queue address not aligned 0x%x\n", (unsigned int) queue_device_address);
        return false;
    }

    zero_bytes(virtio_queue_area, VIRTIO_BLK_QUEUE_BYTES);

    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_BLK_QUEUE_ALIGN);
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_BLK_QUEUE_INDEX);
    uint32_t queue_num_max = VIRTIO_READ32(self->mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_num_max < VIRTIO_BLK_QUEUE_SIZE)
    {
        DRIVER_DEBUG("virtio_blk: queue too small max=%i\n", (int) queue_num_max);
        return false;
    }

    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NUM, VIRTIO_BLK_QUEUE_SIZE);
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_BLK_QUEUE_ALIGN);
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_PFN, queue_device_address / VIRTIO_BLK_QUEUE_ALIGN);

    self->last_used_idx = VirtioBlk__used()->idx;
    self->next_submit_sequence = 0;
    self->next_delivery_sequence = 0;
    for (uint64_t i = 0; i < VIRTIO_BLK_MAX_IN_FLIGHT; i++)
    {
        self->in_flight[i] = (VirtioBlkPendingRequest) { 0 };
    }
    self->pending_read_index = 0;
    self->pending_write_index = 0;
    self->pending_count = 0;
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    return true;
}

static void VirtioBlk__ack_interrupt(void * mmio)
{
    uint32_t status = VIRTIO_READ32(mmio, VIRTIO_MMIO_INTERRUPT_STAT);
    __mmio_sync_boundary();
    DRIVER_DEBUG("virtio_blk: irq status=0x%x\n", status);
    if (status != 0)
    {
        VIRTIO_WRITE32(mmio, VIRTIO_MMIO_INTERRUPT_ACK, status);
        __mmio_sync_boundary();
        DRIVER_DEBUG("virtio_blk: irq acked status=0x%x\n", status);
    }
}

static bool VirtioBlk__pending_enqueue(VirtioBlkDevicePtr self, uint64_t request_id)
{
    if (self->pending_count >= VIRTIO_BLK_PENDING_QUEUE_SIZE)
    {
        DRIVER_DEBUG("virtio_blk: pending queue full id=%i\n", (int) request_id);
        return false;
    }

    self->pending_ids[self->pending_write_index] = request_id;
    self->pending_write_index = (self->pending_write_index + 1) % VIRTIO_BLK_PENDING_QUEUE_SIZE;
    self->pending_count++;
    return true;
}

static bool VirtioBlk__pending_dequeue(VirtioBlkDevicePtr self, uint64_t * request_id)
{
    if (self->pending_count == 0)
    {
        return false;
    }

    *request_id = self->pending_ids[self->pending_read_index];
    self->pending_read_index = (self->pending_read_index + 1) % VIRTIO_BLK_PENDING_QUEUE_SIZE;
    self->pending_count--;
    return true;
}

static bool VirtioBlk__free_slot(VirtioBlkDevicePtr self, VirtioBlkPendingRequestPtr * target)
{
    for (uint64_t i = 0; i < VIRTIO_BLK_MAX_IN_FLIGHT; i++)
    {
        if (!self->in_flight[i].active)
        {
            *target = &self->in_flight[i];
            return true;
        }
    }

    return false;
}

static bool VirtioBlk__slot_for_descriptor(
    VirtioBlkDevicePtr self, uint32_t descriptor_head, VirtioBlkPendingRequestPtr * target
)
{
    if (descriptor_head % VIRTIO_BLK_DESCS_PER_REQUEST != 0)
    {
        return false;
    }

    uint32_t slot_index = descriptor_head / VIRTIO_BLK_DESCS_PER_REQUEST;
    if (slot_index >= VIRTIO_BLK_MAX_IN_FLIGHT || !self->in_flight[slot_index].active)
    {
        return false;
    }

    *target = &self->in_flight[slot_index];
    return true;
}

static uint16_t VirtioBlk__descriptor_head_for_slot(VirtioBlkDevicePtr self, VirtioBlkPendingRequestPtr slot)
{
    uint64_t slot_index = ((uint64_t) slot - (uint64_t) self->in_flight) / sizeof(VirtioBlkPendingRequest);
    return (uint16_t) (slot_index * VIRTIO_BLK_DESCS_PER_REQUEST);
}

static void VirtioBlk__mark_request_failed(
    VirtioBlkDevicePtr self, VirtioBlkPendingRequestPtr slot, uint64_t request_id, ServiceCode status
)
{
    slot->active = true;
    slot->completed = true;
    slot->request_id = request_id;
    slot->submit_sequence = self->next_submit_sequence++;
    slot->block_count = 0;
    slot->service_status = status;
    slot->completed_blocks = 0;
    slot->status = VIRTIO_BLK_S_IOERR;
}

static void VirtioBlk__submit_request(
    VirtioBlkDevicePtr self, VirtioBlkPendingRequestPtr slot, ServiceBlockRequestPtr request
)
{
    uint16_t descriptor_head = VirtioBlk__descriptor_head_for_slot(self, slot);
    uint16_t descriptor_buffer = descriptor_head + 1;
    uint16_t descriptor_status = descriptor_head + 2;

    uint64_t header_device_address = 0;
    uint64_t buffer_device_address = 0;
    uint64_t status_device_address = 0;
    if (!service_ok(service_device_address(&slot->header, &header_device_address))
        || !service_ok(service_device_address(request->buffer, &buffer_device_address))
        || !service_ok(service_device_address(&slot->status, &status_device_address)))
    {
        VirtioBlk__mark_request_failed(self, slot, request->request_id, SERVICE_INVALID_BUFFER);
        return;
    }

    slot->active = true;
    slot->completed = false;
    slot->request_id = request->request_id;
    slot->submit_sequence = self->next_submit_sequence++;
    slot->block_count = request->block_count;
    slot->service_status = SERVICE_EVENT_QUEUE_EMPTY;
    slot->completed_blocks = 0;
    slot->status = VIRTIO_BLK_S_IOERR;
    slot->header = (VirtioBlkOuthdr) {
        .type = request->operation == SERVICE_BLOCK_WRITE ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN,
        .reserved = 0,
        .sector = request->block * (request->block_size / 512),
    };

    VirtqDesc * descs = VirtioBlk__descs();
    VirtqAvail * avail = VirtioBlk__avail();
    VirtqUsed * used = VirtioBlk__used();

    DRIVER_DEBUG(
        "virtio_blk: submit request id=%i op=%i seq=%i desc=%i sector=%i header_dev=0x%x buffer_dev=0x%x status_dev=0x%x avail=%i used=%i last=%i\n",
        (int) request->request_id,
        (int) request->operation,
        (int) slot->submit_sequence,
        (int) descriptor_head,
        (int) slot->header.sector,
        (unsigned int) header_device_address,
        (unsigned int) buffer_device_address,
        (unsigned int) status_device_address,
        (int) avail->idx,
        (int) used->idx,
        (int) self->last_used_idx
    );

    descs[descriptor_head] = (VirtqDesc) {
        .addr = header_device_address,
        .len = sizeof(VirtioBlkOuthdr),
        .flags = VIRTQ_DESC_F_NEXT,
        .next = descriptor_buffer,
    };
    descs[descriptor_buffer] = (VirtqDesc) {
        .addr = buffer_device_address,
        .len = (uint32_t) request->buffer_size,
        .flags = request->operation == SERVICE_BLOCK_READ ? (VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE) : VIRTQ_DESC_F_NEXT,
        .next = descriptor_status,
    };
    descs[descriptor_status] = (VirtqDesc) {
        .addr = status_device_address,
        .len = 1,
        .flags = VIRTQ_DESC_F_WRITE,
        .next = 0,
    };

    avail->ring[avail->idx % VIRTIO_BLK_QUEUE_SIZE] = descriptor_head;
    __mmio_sync_boundary();
    avail->idx++;
    __mmio_sync_boundary();
    DRIVER_DEBUG(
        "virtio_blk: notify request id=%i desc=%i avail=%i used=%i last=%i ring_slot=%i\n",
        (int) request->request_id,
        (int) descriptor_head,
        (int) avail->idx,
        (int) used->idx,
        (int) self->last_used_idx,
        (int) ((avail->idx - 1) % VIRTIO_BLK_QUEUE_SIZE)
    );
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_BLK_QUEUE_INDEX);
}

static void VirtioBlk__flush_completed(VirtioBlkDevicePtr self)
{
    bool flushed = true;
    while (flushed)
    {
        flushed = false;
        for (uint64_t i = 0; i < VIRTIO_BLK_MAX_IN_FLIGHT; i++)
        {
            VirtioBlkPendingRequestPtr slot = &self->in_flight[i];
            if (slot->active && slot->completed && slot->submit_sequence == self->next_delivery_sequence)
            {
                DRIVER_DEBUG(
                    "virtio_blk: deliver request id=%i seq=%i status=%i completed=%i\n",
                    (int) slot->request_id,
                    (int) slot->submit_sequence,
                    (int) slot->service_status,
                    (int) slot->completed_blocks
                );
                service_block_complete(slot->request_id, slot->service_status, slot->completed_blocks);
                *slot = (VirtioBlkPendingRequest) { 0 };
                self->next_delivery_sequence++;
                flushed = true;
                break;
            }
        }
    }
}

static void VirtioBlk__drain_used(VirtioBlkDevicePtr self)
{
    VirtqUsed * used = VirtioBlk__used();
    while (self->last_used_idx != used->idx)
    {
        uint16_t used_index = self->last_used_idx % VIRTIO_BLK_QUEUE_SIZE;
        VirtqUsedElem elem = used->ring[used_index];
        VirtioBlkPendingRequestPtr slot;
        if (!VirtioBlk__slot_for_descriptor(self, elem.id, &slot))
        {
            DRIVER_DEBUG(
                "virtio_blk: used unknown desc=%i len=%i last=%i used=%i\n",
                (int) elem.id,
                (int) elem.len,
                (int) self->last_used_idx,
                (int) used->idx
            );
        }
        else
        {
            slot->completed = true;
            slot->service_status = slot->status == VIRTIO_BLK_S_OK ? SERVICE_OK : SERVICE_BLOCK_REQUEST_FAILED;
            slot->completed_blocks = service_ok(slot->service_status) ? slot->block_count : 0;
            DRIVER_DEBUG(
                "virtio_blk: used request id=%i seq=%i desc=%i len=%i status=%i last=%i used=%i\n",
                (int) slot->request_id,
                (int) slot->submit_sequence,
                (int) elem.id,
                (int) elem.len,
                (int) slot->status,
                (int) self->last_used_idx,
                (int) used->idx
            );
        }

        self->last_used_idx++;
    }
}

static void VirtioBlk__pump_pending(VirtioBlkDevicePtr self)
{
    while (self->pending_count > 0)
    {
        VirtioBlkPendingRequestPtr slot;
        if (!VirtioBlk__free_slot(self, &slot))
        {
            return;
        }

        uint64_t request_id = 0;
        if (!VirtioBlk__pending_dequeue(self, &request_id))
        {
            return;
        }

        ServiceBlockRequest request;
        ServiceCode request_status = service_block_get_request(request_id, &request);
        if (!service_ok(request_status))
        {
            DRIVER_DEBUG("virtio_blk: block request %i fetch failed %i\n", (int) request_id, (int) request_status);
            VirtioBlk__mark_request_failed(self, slot, request_id, request_status);
            VirtioBlk__flush_completed(self);
            continue;
        }

        DRIVER_DEBUG(
            "virtio_blk: block request id=%i op=%i block=%i count=%i buffer=%p size=%i\n",
            (int) request.request_id,
            (int) request.operation,
            (int) request.block,
            (int) request.block_count,
            request.buffer,
            (int) request.buffer_size
        );

        if (request.operation != SERVICE_BLOCK_READ
            && request.operation != SERVICE_BLOCK_WRITE)
        {
            VirtioBlk__mark_request_failed(self, slot, request.request_id, SERVICE_BLOCK_REQUEST_FAILED);
            VirtioBlk__flush_completed(self);
            continue;
        }

        if (request.block_count == 0
            || request.buffer_size < request.block_count * request.block_size)
        {
            VirtioBlk__mark_request_failed(self, slot, request.request_id, SERVICE_INVALID_BUFFER);
            VirtioBlk__flush_completed(self);
            continue;
        }

        VirtioBlk__submit_request(self, slot, &request);
        VirtioBlk__flush_completed(self);
    }
}

static void VirtioBlk__handle_block_request(VirtioBlkDevicePtr self, uint64_t request_id)
{
    if (!VirtioBlk__pending_enqueue(self, request_id))
    {
        service_block_complete(request_id, SERVICE_BLOCK_REQUEST_FAILED, 0);
        return;
    }

    VirtioBlk__pump_pending(self);
}

static void VirtioBlk__handle_irq(VirtioBlkDevicePtr self)
{
    VirtioBlk__ack_interrupt(self->mmio);
    VirtioBlk__drain_used(self);
    VirtioBlk__flush_completed(self);
    VirtioBlk__pump_pending(self);
}

int VIRTIO_BLK_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("virtio_blk: service registration failed\n");
        return 1;
    }

    VirtioBlkDevice device;
    if (!VirtioBlk__find_device(&device))
    {
        DRIVER_DEBUG("virtio_blk: block device not found\n");
        return 1;
    }

    VirtioBlk__acknowledge_driver(device.mmio);
    if (!VirtioBlk__setup_queue(&device))
    {
        DRIVER_DEBUG("virtio_blk: queue setup failed\n");
        return 1;
    }
    VIRTIO_BLK_TEST_DEVICE_READY(&device);

    if (!service_ok(service_register_block_device("/dev/virtio1", 512, 0)))
    {
        DRIVER_DEBUG("virtio_blk: block device registration failed\n");
        return 1;
    }

    if (!service_ok(service_subscribe_irq(device.irq_id)))
    {
        DRIVER_DEBUG("virtio_blk: irq subscribe failed\n");
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode wait_status = service_wait(&event);
         service_alive(wait_status);
         wait_status = service_wait(&event))
    {
        if (service_ok(wait_status))
        {
            DRIVER_DEBUG(
                "virtio_blk: main event type=%i source=%i data=%i used=%i last=%i\n",
                (int) event.type,
                (int) event.source,
                (int) event.data,
                (int) VirtioBlk__used()->idx,
                (int) device.last_used_idx
            );
            __mmio_sync_boundary();
            bool virtio_irq = event.type == SERVICE_EVENT_IRQ && event.source == device.irq_id;
            if (virtio_irq)
            {
                VirtioBlk__handle_irq(&device);
            }

            if (event.type == SERVICE_EVENT_BLOCK_IO)
            {
                VirtioBlk__handle_block_request(&device, event.source);
            }
            __mmio_sync_boundary();
        }
    }

    return 0;
}

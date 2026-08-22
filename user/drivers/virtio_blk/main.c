#define DRIVER_LOG_TAG "VIRTIO-BLK"

#include "../../sys/debug.h"
#include "../../sys/service.h"
#include "../../sys/sync.h"

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

#define VIRTIO_MMIO_MAGIC           0x000
#define VIRTIO_MMIO_VERSION         0x004
#define VIRTIO_MMIO_DEVICE_ID       0x008
#define VIRTIO_MMIO_VENDOR_ID       0x00c
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL       0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX   0x034
#define VIRTIO_MMIO_QUEUE_NUM       0x038
#define VIRTIO_MMIO_QUEUE_ALIGN     0x03c
#define VIRTIO_MMIO_QUEUE_PFN       0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY    0x050
#define VIRTIO_MMIO_INTERRUPT_STAT  0x060
#define VIRTIO_MMIO_INTERRUPT_ACK   0x064
#define VIRTIO_MMIO_STATUS          0x070
#define VIRTIO_MMIO_CONFIG          0x100

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK  2U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER      2U
#define VIRTIO_STATUS_DRIVER_OK   4U

#define VIRTIO_BLK_QUEUE_INDEX 0U
#define VIRTIO_BLK_QUEUE_SIZE  8U
#define VIRTIO_BLK_QUEUE_ALIGN 4096U
#define VIRTIO_BLK_QUEUE_BYTES 8192U

#define VIRTQ_DESC_F_NEXT  1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN  0U
#define VIRTIO_BLK_T_OUT 1U

#define VIRTIO_BLK_S_OK    0U
#define VIRTIO_BLK_S_IOERR 1U

typedef struct VirtioBlkDevice
{
    ServiceMappedMemoryPtr mapping;
    void * mmio;
    uint64_t slot;
    uint64_t irq_id;
    uint64_t block_count;
    char path[SERVICE_DEVICE_PATH_MAX];
    uint16_t last_used_idx;
}
VirtioBlkDevice, * VirtioBlkDevicePtr;

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

static uint8_t virtio_queue_area[VIRTIO_BLK_QUEUE_BYTES] __attribute__((aligned(VIRTIO_BLK_QUEUE_ALIGN)));
static VirtioBlkOuthdr virtio_blk_header;
static uint8_t virtio_blk_status;

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

static VirtqDesc * VirtioBlk__descs(VirtioBlkDevicePtr self)
{
    return (VirtqDesc *) virtio_queue_area;
}

static VirtqAvail * VirtioBlk__avail(VirtioBlkDevicePtr self)
{
    return (VirtqAvail *) (virtio_queue_area + sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE);
}

static VirtqUsed * VirtioBlk__used(VirtioBlkDevicePtr self)
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

static uint64_t VirtioBlk__read_u64_config(void * mmio, uint64_t offset)
{
    uint64_t low = VIRTIO_READ32(mmio, VIRTIO_MMIO_CONFIG + offset);
    uint64_t high = VIRTIO_READ32(mmio, VIRTIO_MMIO_CONFIG + offset + 4);
    return low | (high << 32);
}

static void VirtioBlk__copy_string(char target[SERVICE_DEVICE_PATH_MAX], const char * source)
{
    uint64_t i = 0;
    if (source != 0)
    {
        for (; source[i] != '\0' && i < SERVICE_DEVICE_PATH_MAX - 1; i++)
        {
            target[i] = source[i];
        }
    }
    target[i] = '\0';
}

static bool VirtioBlk__parse_u64(const char * value, uint64_t * target)
{
    if (value == 0 || value[0] == '\0')
    {
        return false;
    }

    uint64_t parsed = 0;
    for (uint64_t i = 0; value[i] != '\0'; i++)
    {
        if (value[i] < '0' || value[i] > '9')
        {
            return false;
        }

        parsed = (parsed * 10) + (uint64_t) (value[i] - '0');
    }

    *target = parsed;
    return true;
}

static bool VirtioBlk__bind_device(VirtioBlkDevicePtr self, uint64_t slot, const char * path)
{
    if (slot >= VIRTIO_MMIO_SLOT_COUNT)
    {
        DRIVER_DEBUG("virtio_blk: slot out of range %i\n", (int) slot);
        return false;
    }

    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(VIRTIO_MMIO_FIRST_PHYS_BASE, VIRTIO_MMIO_REGION_SIZE, &mapping)))
    {
        DRIVER_DEBUG("virtio_blk: mmio region map failed\n");
        return false;
    }

    uint64_t physical_address = VIRTIO_MMIO_FIRST_PHYS_BASE + (slot * VIRTIO_MMIO_SLOT_SIZE);
    void * mmio = driver_ptr_offset(mapping, slot * VIRTIO_MMIO_SLOT_SIZE);
    if (!VirtioBlk__probe(mmio, slot, physical_address))
    {
        DRIVER_DEBUG("virtio_blk: requested slot %i is not a block device\n", (int) slot);
        return false;
    }

    *self = (VirtioBlkDevice) {
        .mapping = mapping,
        .mmio = mmio,
        .slot = slot,
        .irq_id = VIRTIO_MMIO_FIRST_IRQ_ID + slot,
        .block_count = VirtioBlk__read_u64_config(mmio, 0),
        .last_used_idx = 0,
    };
    VirtioBlk__copy_string(self->path, path);
    DRIVER_DEBUG(
        "virtio_blk: discovered path=%s slot=%i irq=%i block_count=%i\n",
        self->path,
        (int) self->slot,
        (int) self->irq_id,
        (int) self->block_count
    );
    return true;
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
    virtio_blk_header = (VirtioBlkOuthdr) { 0 };
    virtio_blk_status = 0;

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

    self->last_used_idx = VirtioBlk__used(self)->idx;
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    return true;
}

static void VirtioBlk__ack_interrupt(void * mmio)
{
    uint32_t status = VIRTIO_READ32(mmio, VIRTIO_MMIO_INTERRUPT_STAT);
    __sync_boundary();
    DRIVER_DEBUG("virtio_blk: irq status=0x%x\n", status);
    if (status != 0)
    {
        VIRTIO_WRITE32(mmio, VIRTIO_MMIO_INTERRUPT_ACK, status);
        __sync_boundary();
        DRIVER_DEBUG("virtio_blk: irq acked status=0x%x\n", status);
    }
}

static bool VirtioBlk__wait_for_used(VirtioBlkDevicePtr self)
{
    ServiceEvent event;
    DRIVER_DEBUG(
        "virtio_blk: wait used start last=%i used=%i irq=%i\n",
        (int) self->last_used_idx,
        (int) VirtioBlk__used(self)->idx,
        (int) self->irq_id
    );

    while (VirtioBlk__used(self)->idx == self->last_used_idx)
    {
        ServiceCode wait_status = service_wait(&event);
        DRIVER_DEBUG(
            "virtio_blk: wait returned status=%i event_type=%i source=%i data=%i used=%i last=%i\n",
            (int) wait_status,
            (int) event.type,
            (int) event.source,
            (int) event.data,
            (int) VirtioBlk__used(self)->idx,
            (int) self->last_used_idx
        );
        if (!service_alive(wait_status))
        {
            DRIVER_DEBUG("virtio_blk: wait aborted status=%i\n", (int) wait_status);
            return false;
        }

        if (service_ok(wait_status) && event.type == SERVICE_EVENT_IRQ && event.source == self->irq_id)
        {
            VirtioBlk__ack_interrupt(self->mmio);
        }
    }

    self->last_used_idx = VirtioBlk__used(self)->idx;
    DRIVER_DEBUG(
        "virtio_blk: wait used done last=%i status=%i\n",
        (int) self->last_used_idx,
        (int) virtio_blk_status
    );
    return true;
}

static ServiceCode VirtioBlk__execute_request(VirtioBlkDevicePtr self, ServiceBlockRequestPtr request)
{
    if (request->block_count == 0 || request->buffer == 0 || request->buffer_size < request->block_count * request->block_size)
    {
        return SERVICE_INVALID_BUFFER;
    }

    uint64_t header_device_address = 0;
    uint64_t buffer_device_address = 0;
    uint64_t status_device_address = 0;
    if (!service_ok(service_device_address(&virtio_blk_header, &header_device_address))
        || !service_ok(service_device_address(request->buffer, &buffer_device_address))
        || !service_ok(service_device_address(&virtio_blk_status, &status_device_address)))
    {
        return SERVICE_INVALID_BUFFER;
    }

    virtio_blk_status = VIRTIO_BLK_S_IOERR;
    virtio_blk_header = (VirtioBlkOuthdr) {
        .type = request->operation == SERVICE_BLOCK_WRITE ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN,
        .reserved = 0,
        .sector = request->block * (request->block_size / 512),
    };

    VirtqDesc * descs = VirtioBlk__descs(self);
    VirtqAvail * avail = VirtioBlk__avail(self);
    VirtqUsed * used = VirtioBlk__used(self);

    DRIVER_DEBUG(
        "virtio_blk: submit request id=%i op=%i sector=%i header_drv=%p buffer_drv=%p status_drv=%p "
        "header_dev=0x%x buffer_dev=0x%x status_dev=0x%x avail=%i used=%i last=%i\n",
        (int) request->request_id,
        (int) request->operation,
        (int) virtio_blk_header.sector,
        &virtio_blk_header,
        request->buffer,
        &virtio_blk_status,
        (unsigned int) header_device_address,
        (unsigned int) buffer_device_address,
        (unsigned int) status_device_address,
        (int) avail->idx,
        (int) used->idx,
        (int) self->last_used_idx
    );

    descs[0] = (VirtqDesc) {
        .addr = header_device_address,
        .len = sizeof(VirtioBlkOuthdr),
        .flags = VIRTQ_DESC_F_NEXT,
        .next = 1,
    };
    descs[1] = (VirtqDesc) {
        .addr = buffer_device_address,
        .len = (uint32_t) request->buffer_size,
        .flags = request->operation == SERVICE_BLOCK_READ ? (VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE) : VIRTQ_DESC_F_NEXT,
        .next = 2,
    };
    descs[2] = (VirtqDesc) {
        .addr = status_device_address,
        .len = 1,
        .flags = VIRTQ_DESC_F_WRITE,
        .next = 0,
    };

    avail->ring[avail->idx % VIRTIO_BLK_QUEUE_SIZE] = 0;
    __sync_boundary();
    avail->idx++;
    __sync_boundary();
    DRIVER_DEBUG(
        "virtio_blk: notify request id=%i avail=%i used=%i last=%i ring_slot=%i\n",
        (int) request->request_id,
        (int) avail->idx,
        (int) used->idx,
        (int) self->last_used_idx,
        (int) ((avail->idx - 1) % VIRTIO_BLK_QUEUE_SIZE)
    );
    VIRTIO_WRITE32(self->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_BLK_QUEUE_INDEX);

    if (!VirtioBlk__wait_for_used(self))
    {
        DRIVER_DEBUG("virtio_blk: request id=%i wait failed\n", (int) request->request_id);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    if (request->operation == SERVICE_BLOCK_READ && request->buffer_size >= 8)
    {
        uint8_t * buffer = (uint8_t *) request->buffer;
        DRIVER_DEBUG(
            "virtio_blk: read request id=%i sector=%i first_bytes=%i,%i,%i,%i,%i,%i,%i,%i\n",
            (int) request->request_id,
            (int) virtio_blk_header.sector,
            (int) buffer[0],
            (int) buffer[1],
            (int) buffer[2],
            (int) buffer[3],
            (int) buffer[4],
            (int) buffer[5],
            (int) buffer[6],
            (int) buffer[7]
        );
    }

    DRIVER_DEBUG(
        "virtio_blk: request id=%i completed virtio_status=%i used=%i last=%i\n",
        (int) request->request_id,
        (int) virtio_blk_status,
        (int) used->idx,
        (int) self->last_used_idx
    );
    return virtio_blk_status == VIRTIO_BLK_S_OK ? SERVICE_OK : SERVICE_BLOCK_REQUEST_FAILED;
}

static bool VirtioBlk__paths_equal(const char * left, const char * right)
{
    uint64_t i = 0;
    while (left[i] != '\0' && right[i] != '\0')
    {
        if (left[i] != right[i])
        {
            return false;
        }
        i++;
    }

    return left[i] == right[i];
}

static void VirtioBlk__handle_block_request(VirtioBlkDevicePtr self, uint64_t request_id)
{
    ServiceBlockRequest request;
    ServiceCode request_status = service_block_get_request(request_id, &request);
    if (!service_ok(request_status))
    {
        DRIVER_DEBUG("virtio_blk: block request %i fetch failed %i\n", (int) request_id, (int) request_status);
        service_block_complete(request_id, request_status, 0);
        return;
    }

    if (!VirtioBlk__paths_equal(request.device_path, self->path))
    {
        DRIVER_DEBUG(
            "virtio_blk: no device for request id=%i path=%s active_path=%s\n",
            (int) request.request_id,
            request.device_path,
            self->path
        );
        service_block_complete(request.request_id, SERVICE_BLOCK_REQUEST_FAILED, 0);
        return;
    }

    DRIVER_DEBUG(
        "virtio_blk: route request id=%i req_path=%s dev_path=%s slot=%i irq=%i block_count=%i\n",
        (int) request.request_id,
        request.device_path,
        self->path,
        (int) self->slot,
        (int) self->irq_id,
        (int) self->block_count
    );

    DRIVER_DEBUG(
        "virtio_blk: block request id=%i path=%s op=%i block=%i count=%i buffer=%p size=%i\n",
        (int) request.request_id,
        request.device_path,
        (int) request.operation,
        (int) request.block,
        (int) request.block_count,
        request.buffer,
        (int) request.buffer_size
    );

    if (request.operation != SERVICE_BLOCK_READ && request.operation != SERVICE_BLOCK_WRITE)
    {
        service_block_complete(request.request_id, SERVICE_BLOCK_REQUEST_FAILED, 0);
        return;
    }

    ServiceCode status = VirtioBlk__execute_request(self, &request);
    uint64_t completed_blocks = service_ok(status) ? request.block_count : 0;
    DRIVER_DEBUG(
        "virtio_blk: block complete id=%i status=%i completed=%i\n",
        (int) request.request_id,
        (int) status,
        (int) completed_blocks
    );
    service_block_complete(request.request_id, status, completed_blocks);
}

int VIRTIO_BLK_ENTRYPOINT(int argc, char ** argv)
{
    if (argc < 3)
    {
        DRIVER_DEBUG("virtio_blk: expected slot and path arguments\n");
        return 1;
    }

    uint64_t slot = 0;
    if (!VirtioBlk__parse_u64(argv[1], &slot))
    {
        DRIVER_DEBUG("virtio_blk: invalid slot argument %s\n", argv[1]);
        return 1;
    }

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("virtio_blk: service registration failed\n");
        return 1;
    }

    VirtioBlkDevice device = { 0 };
    if (!VirtioBlk__bind_device(&device, slot, argv[2]))
    {
        DRIVER_DEBUG("virtio_blk: block device bind failed slot=%i path=%s\n", (int) slot, argv[2]);
        return 1;
    }

    VirtioBlk__acknowledge_driver(device.mmio);
    if (!VirtioBlk__setup_queue(&device))
    {
        DRIVER_DEBUG("virtio_blk: queue setup failed path=%s\n", device.path);
        return 1;
    }
    VIRTIO_BLK_TEST_DEVICE_READY(&device);

    if (!service_ok(service_register_block_device(device.path, 512, device.block_count)))
    {
        DRIVER_DEBUG("virtio_blk: block device registration failed path=%s\n", device.path);
        return 1;
    }

    BString ready_path = bstring(device.path);
    if (!service_ok(service_publish_ready(&ready_path)))
    {
        DRIVER_DEBUG("virtio_blk: ready publish failed path=%s\n", device.path);
        return 1;
    }

    if (!service_ok(service_subscribe_irq(device.irq_id)))
    {
        DRIVER_DEBUG("virtio_blk: irq subscribe failed path=%s\n", device.path);
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
                (int) VirtioBlk__used(&device)->idx,
                (int) device.last_used_idx
            );
            __sync_boundary();
            if (event.type == SERVICE_EVENT_IRQ && event.source == device.irq_id)
            {
                VirtioBlk__ack_interrupt(device.mmio);
            }

            if (event.type == SERVICE_EVENT_BLOCK_IO)
            {
                VirtioBlk__handle_block_request(&device, event.source);
            }
            __sync_boundary();
        }
    }

    return 0;
}

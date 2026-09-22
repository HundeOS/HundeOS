#define DRIVER_LOG_TAG "VIRTIO-BLK"

#include "sys/debug.h"
#include "sys/boot_info.h"
#include "sys/service.h"
#include "sys/sync.h"
#include "channel/signal.h"
#include "channel/block_server.h"
#include "buffer/client.h"

#include "device.h"
#include "channelio.h"

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
#define VIRTIO_MMIO_VENDOR_ID 0x00c
#define VIRTIO_MMIO_GUEST_PAGE_SIZE 0x028
#define VIRTIO_MMIO_QUEUE_SEL 0x030
#define VIRTIO_MMIO_QUEUE_NUM_MAX 0x034
#define VIRTIO_MMIO_QUEUE_NUM 0x038
#define VIRTIO_MMIO_QUEUE_ALIGN 0x03c
#define VIRTIO_MMIO_QUEUE_PFN 0x040
#define VIRTIO_MMIO_QUEUE_NOTIFY 0x050
#define VIRTIO_MMIO_INTERRUPT_STAT 0x060
#define VIRTIO_MMIO_INTERRUPT_ACK 0x064
#define VIRTIO_MMIO_STATUS 0x070
#define VIRTIO_MMIO_CONFIG 0x100

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK 2U
#define VIRTIO_STATUS_ACKNOWLEDGE 1U
#define VIRTIO_STATUS_DRIVER 2U
#define VIRTIO_STATUS_DRIVER_OK 4U

#define VIRTIO_BLK_QUEUE_INDEX 0U
#define VIRTIO_BLK_QUEUE_SIZE 8U
#define VIRTIO_BLK_QUEUE_ALIGN 4096U
#define VIRTIO_BLK_QUEUE_BYTES 8192U

#define VIRTQ_DESC_F_NEXT 1U
#define VIRTQ_DESC_F_WRITE 2U

#define VIRTIO_BLK_T_IN 0U
#define VIRTIO_BLK_T_OUT 1U

#define VIRTIO_BLK_S_OK 0U
#define VIRTIO_BLK_S_IOERR 1U

typedef struct VirtqDesc
{
    uint64_t addr;
    uint32_t len;
    uint16_t flags;
    uint16_t next;
} VirtqDesc;

typedef VirtqDesc * VirtqDescPtr;

typedef struct VirtqAvail
{
    uint16_t flags;
    uint16_t idx;
    uint16_t ring[VIRTIO_BLK_QUEUE_SIZE];
} VirtqAvail;

typedef VirtqAvail * VirtqAvailPtr;

typedef struct VirtqUsedElem
{
    uint32_t id;
    uint32_t len;
} VirtqUsedElem;

typedef struct VirtqUsed
{
    uint16_t flags;
    uint16_t idx;
    VirtqUsedElem ring[VIRTIO_BLK_QUEUE_SIZE];
} VirtqUsed;

typedef VirtqUsed * VirtqUsedPtr;

typedef struct VirtioBlkOuthdr
{
    uint32_t type;
    uint32_t reserved;
    uint64_t sector;
} VirtioBlkOuthdr;

static uint8_t virtio_queue_area[VIRTIO_BLK_QUEUE_BYTES] __attribute__((aligned(VIRTIO_BLK_QUEUE_ALIGN)));
static VirtioBlkOuthdr virtio_blk_header;
static uint8_t virtio_blk_status;

static VirtioBlkDevice VirtioBlkDevice__new(ServiceMappedMemoryPtr mapping, void * mmio, uint64_t slot, uint64_t irq_id,
                                            uint64_t block_count, ConstBStringPtr path)
{
    return (VirtioBlkDevice){
        .mapping = mapping,
        .mmio = mmio,
        .slot = slot,
        .irq_id = irq_id,
        .block_count = block_count,
        .path = *path,
        .last_used_idx = 0,
    };
}

static VirtqDesc VirtqDesc__new(uint64_t addr, uint32_t len, uint16_t flags, uint16_t next)
{
    return (VirtqDesc){
        .addr = addr,
        .len = len,
        .flags = flags,
        .next = next,
    };
}

static VirtioBlkOuthdr VirtioBlkOuthdr__new(uint32_t type, uint64_t sector)
{
    return (VirtioBlkOuthdr){
        .type = type,
        .reserved = 0,
        .sector = sector,
    };
}

static void zero_bytes(void * target, uint64_t size)
{
    uint8_t * bytes = (uint8_t *)target;
    for (uint64_t i = 0; i < size; i++)
    {
        bytes[i] = 0;
    }
}

static uint64_t align_up(uint64_t value, uint64_t alignment)
{
    return (value + alignment - 1) & ~(alignment - 1);
}

static VirtqDescPtr VirtioBlk__descs(VirtioBlkDevicePtr self)
{
    return (VirtqDescPtr)virtio_queue_area;
}

static VirtqAvailPtr VirtioBlk__avail(VirtioBlkDevicePtr self)
{
    return (VirtqAvailPtr)(virtio_queue_area + sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE);
}

static VirtqUsedPtr VirtioBlk__used(VirtioBlkDevicePtr self)
{
    uint64_t used_offset =
        align_up(sizeof(VirtqDesc) * VIRTIO_BLK_QUEUE_SIZE + sizeof(VirtqAvail), VIRTIO_BLK_QUEUE_ALIGN);
    return (VirtqUsedPtr)(virtio_queue_area + used_offset);
}

static void VirtioBlk__clean_queue(void)
{
    __sync_cache_clean(virtio_queue_area, VIRTIO_BLK_QUEUE_BYTES);
}

static void VirtioBlk__invalidate_used(VirtioBlkDevicePtr self)
{
    __sync_cache_invalidate(VirtioBlk__used(self), sizeof(VirtqUsed));
}

static bool VirtioBlk__probe(void * mmio, uint64_t slot, uint64_t physical_address)
{
    uint32_t magic = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_MAGIC);
    uint32_t version = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_VERSION);
    uint32_t device_id = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_DEVICE_ID);
    uint32_t vendor_id = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_VENDOR_ID);

    DRIVER_DEBUG("virtio_blk: probe slot=%i base=0x%x magic=0x%x version=%i device_id=%i vendor=0x%x\n", (int)slot,
                 (unsigned int)physical_address, magic, (int)version, (int)device_id, vendor_id);

    return magic == VIRTIO_MMIO_MAGIC_VALUE && version >= 1 && device_id == VIRTIO_DEVICE_ID_BLOCK;
}

static uint64_t VirtioBlk__read_u64_config(void * mmio, uint64_t offset)
{
    uint64_t low = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_CONFIG + offset);
    uint64_t high = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_CONFIG + offset + 4);
    return low | (high << 32);
}

static Result(VirtioBlkDevice) VirtioBlk__bind_device(uint64_t slot, ConstBStringPtr path)
{
    BootVirtioMmioInfo virtio_mmio;
    match_result(
        BootHardwareInfo__load(),
        success(info)
        {
            match_optional(
                info.virtio_mmio,
                some(value)
                {
                    virtio_mmio = value;
                },
                none()
                {
                    return Error(VirtioBlkDevice, bstr("VirtIO MMIO information unavailable"),
                                 SERVICE_BLOCK_REQUEST_FAILED);
                }
            );
        },
        error(code, message)
        {
            return Error(VirtioBlkDevice, message, SERVICE_BLOCK_REQUEST_FAILED);
        }
    );
    if (slot >= virtio_mmio.slot_count)
    {
        DRIVER_DEBUG("virtio_blk: slot out of range %i\n", (int)slot);
        return Error(VirtioBlkDevice, bstr("Cannot bind VirtIO device"), SERVICE_BLOCK_REQUEST_FAILED);
    }

    ServiceMappedMemoryPtr mapping;
    if (!service_ok(
            service_map_mmio(virtio_mmio.physical_base, virtio_mmio.slot_size * virtio_mmio.slot_count, &mapping)))
    {
        DRIVER_DEBUG("virtio_blk: mmio region map failed\n");
        return Error(VirtioBlkDevice, bstr("Cannot bind VirtIO device"), SERVICE_BLOCK_REQUEST_FAILED);
    }

    uint64_t physical_address = virtio_mmio.physical_base + (slot * virtio_mmio.slot_size);
    void * mmio = driver_ptr_offset(mapping, slot * virtio_mmio.slot_size);
    if (!VirtioBlk__probe(mmio, slot, physical_address))
    {
        DRIVER_DEBUG("virtio_blk: requested slot %i is not a block device\n", (int)slot);
        return Error(VirtioBlkDevice, bstr("Cannot bind VirtIO device"), SERVICE_BLOCK_REQUEST_FAILED);
    }

    VirtioBlkDevice device = VirtioBlkDevice__new(mapping, mmio, slot, virtio_mmio.first_irq + slot,
                                                  VirtioBlk__read_u64_config(mmio, 0), path);
    return Success(VirtioBlkDevice, device);
}

static void VirtioBlk__acknowledge_driver(void * mmio)
{
    VirtioBlkMmio__write32(mmio, VIRTIO_MMIO_STATUS, 0);
    VirtioBlkMmio__write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE);
    VirtioBlkMmio__write32(mmio, VIRTIO_MMIO_STATUS, VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER);
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
        DRIVER_DEBUG("virtio_blk: queue address not aligned 0x%x\n", (unsigned int)queue_device_address);
        return false;
    }

    zero_bytes(virtio_queue_area, VIRTIO_BLK_QUEUE_BYTES);
    virtio_blk_header = VirtioBlkOuthdr__new(0, 0);
    virtio_blk_status = 0;
    __sync_cache_clean(&virtio_blk_header, sizeof(virtio_blk_header));
    __sync_cache_clean(&virtio_blk_status, sizeof(virtio_blk_status));
    VirtioBlk__clean_queue();

    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_GUEST_PAGE_SIZE, VIRTIO_BLK_QUEUE_ALIGN);
    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_QUEUE_SEL, VIRTIO_BLK_QUEUE_INDEX);
    uint32_t queue_num_max = VirtioBlkMmio__read32(self->mmio, VIRTIO_MMIO_QUEUE_NUM_MAX);
    if (queue_num_max < VIRTIO_BLK_QUEUE_SIZE)
    {
        DRIVER_DEBUG("virtio_blk: queue too small max=%i\n", (int)queue_num_max);
        return false;
    }

    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_QUEUE_NUM, VIRTIO_BLK_QUEUE_SIZE);
    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_QUEUE_ALIGN, VIRTIO_BLK_QUEUE_ALIGN);
    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_QUEUE_PFN, queue_device_address / VIRTIO_BLK_QUEUE_ALIGN);

    VirtioBlk__invalidate_used(self);
    self->last_used_idx = VirtioBlk__used(self)->idx;
    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_STATUS,
                           VIRTIO_STATUS_ACKNOWLEDGE | VIRTIO_STATUS_DRIVER | VIRTIO_STATUS_DRIVER_OK);
    return true;
}

void VirtioBlk__ack_interrupt(void * mmio)
{
    uint32_t status = VirtioBlkMmio__read32(mmio, VIRTIO_MMIO_INTERRUPT_STAT);
    __sync_boundary();
    DRIVER_DEBUG("virtio_blk: irq status=0x%x\n", status);
    if (status != 0)
    {
        VirtioBlkMmio__write32(mmio, VIRTIO_MMIO_INTERRUPT_ACK, status);
        __sync_boundary();
        DRIVER_DEBUG("virtio_blk: irq acked status=0x%x\n", status);
    }
}

static bool VirtioBlk__wait_for_used(VirtioBlkDevicePtr self)
{
    ServiceEvent event;
    VirtioBlk__invalidate_used(self);
    DRIVER_DEBUG("virtio_blk: wait used start last=%i used=%i irq=%i\n", (int)self->last_used_idx,
                 (int)VirtioBlk__used(self)->idx, (int)self->irq_id);

    VirtioBlk__invalidate_used(self);
    while (VirtioBlk__used(self)->idx == self->last_used_idx)
    {
        ServiceCode wait_status = VirtioBlkRuntime__await_irq(&event);
        VirtioBlk__invalidate_used(self);
        DRIVER_DEBUG("virtio_blk: wait returned status=%i event_type=%i source=%i data=%i used=%i last=%i\n",
                     (int)wait_status, (int)event.type, (int)event.source, (int)event.data,
                     (int)VirtioBlk__used(self)->idx, (int)self->last_used_idx);
        if (!service_alive(wait_status))
        {
            DRIVER_DEBUG("virtio_blk: wait aborted status=%i\n", (int)wait_status);
            return false;
        }

        if (service_ok(wait_status) && event.type == SERVICE_EVENT_IRQ && event.source == self->irq_id)
        {
            VirtioBlk__ack_interrupt(self->mmio);
            VirtioBlkChannelIO__acknowledge(self->irq_id);
            VirtioBlk__invalidate_used(self);
        }
    }

    self->last_used_idx = VirtioBlk__used(self)->idx;
    __sync_cache_invalidate(&virtio_blk_status, sizeof(virtio_blk_status));
    DRIVER_DEBUG("virtio_blk: wait used done last=%i status=%i\n", (int)self->last_used_idx, (int)virtio_blk_status);
    return true;
}

typedef struct VirtioBlkTransfer
{
    BlockProtocolOperation operation;
    BlockProtocolBlockNumber block;
    BlockProtocolBlockCount block_count;
    Address buffer;
    size_t buffer_size;
} VirtioBlkTransfer;
typedef const VirtioBlkTransfer * ConstVirtioBlkTransferPtr;

static VirtioBlkTransfer VirtioBlkTransfer__new(BlockProtocolOperation operation, BlockProtocolBlockNumber block,
                                                BlockProtocolBlockCount count, Address buffer)
{
    return (VirtioBlkTransfer){.operation = operation,
                               .block = block,
                               .block_count = count,
                               .buffer = buffer,
                               .buffer_size = count * BLOCK_CHANNEL_SECTOR_SIZE};
}

static ServiceCode VirtioBlk__execute_dma(VirtioBlkDevicePtr self, ConstVirtioBlkTransferPtr request,
                                          Address buffer_device_address)
{
    if (self->io_failed || request->block_count == 0 || request->buffer == 0 || request->buffer_size > BUFFER_SIZE ||
        request->block_count != request->buffer_size / BLOCK_CHANNEL_SECTOR_SIZE ||
        request->buffer_size % BLOCK_CHANNEL_SECTOR_SIZE != 0)
    {
        return SERVICE_INVALID_BUFFER;
    }

    uint64_t header_device_address = 0;
    uint64_t status_device_address = 0;
    if (!service_ok(service_device_address(&virtio_blk_header, &header_device_address)) ||
        !service_ok(service_device_address(&virtio_blk_status, &status_device_address)))
    {
        return SERVICE_INVALID_BUFFER;
    }

    virtio_blk_status = VIRTIO_BLK_S_IOERR;
    virtio_blk_header = VirtioBlkOuthdr__new(
        request->operation == BLOCK_PROTOCOL_WRITE ? VIRTIO_BLK_T_OUT : VIRTIO_BLK_T_IN, request->block);

    VirtqDescPtr descs = VirtioBlk__descs(self);
    VirtqAvailPtr avail = VirtioBlk__avail(self);
    VirtqUsedPtr used = VirtioBlk__used(self);

    DRIVER_DEBUG("virtio_blk: submit block=%i op=%i sector=%i header_drv=%p buffer_drv=%p status_drv=%p "
                 "header_dev=0x%x buffer_dev=0x%x status_dev=0x%x avail=%i used=%i last=%i\n",
                 (int)request->block, (int)request->operation, (int)virtio_blk_header.sector, &virtio_blk_header,
                 (void *)request->buffer, &virtio_blk_status, (unsigned int)header_device_address,
                 (unsigned int)buffer_device_address, (unsigned int)status_device_address, (int)avail->idx,
                 (int)used->idx, (int)self->last_used_idx);

    descs[0] = VirtqDesc__new(header_device_address, sizeof(VirtioBlkOuthdr), VIRTQ_DESC_F_NEXT, 1);
    descs[1] = VirtqDesc__new(
        buffer_device_address, (uint32_t)request->buffer_size,
        request->operation == BLOCK_PROTOCOL_READ ? (VIRTQ_DESC_F_NEXT | VIRTQ_DESC_F_WRITE) : VIRTQ_DESC_F_NEXT, 2);
    descs[2] = VirtqDesc__new(status_device_address, 1, VIRTQ_DESC_F_WRITE, 0);

    avail->ring[avail->idx % VIRTIO_BLK_QUEUE_SIZE] = 0;
    if (request->operation == BLOCK_PROTOCOL_WRITE)
    {
        __sync_cache_clean((void *)request->buffer, request->buffer_size);
    }
    else
    {
        __sync_cache_clean_invalidate((void *)request->buffer, request->buffer_size);
    }
    __sync_cache_clean(&virtio_blk_header, sizeof(virtio_blk_header));
    __sync_cache_clean_invalidate(&virtio_blk_status, sizeof(virtio_blk_status));
    VirtioBlk__clean_queue();
    __sync_boundary();
    avail->idx++;
    VirtioBlk__clean_queue();
    __sync_boundary();
    DRIVER_DEBUG("virtio_blk: notify block=%i avail=%i used=%i last=%i ring_slot=%i\n", (int)request->block,
                 (int)avail->idx, (int)used->idx, (int)self->last_used_idx,
                 (int)((avail->idx - 1) % VIRTIO_BLK_QUEUE_SIZE));
    VirtioBlkMmio__write32(self->mmio, VIRTIO_MMIO_QUEUE_NOTIFY, VIRTIO_BLK_QUEUE_INDEX);

    if (!VirtioBlk__wait_for_used(self))
    {
        // Completion is uncertain. Do not reuse the queue for another request.
        self->io_failed = true;
        DRIVER_DEBUG("virtio_blk: block=%i wait failed\n", (int)request->block);
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    __sync_cache_invalidate(&virtio_blk_status, sizeof(virtio_blk_status));
    if (request->operation == BLOCK_PROTOCOL_READ)
    {
        __sync_cache_invalidate((void *)request->buffer, request->buffer_size);
    }

    if (request->operation == BLOCK_PROTOCOL_READ && request->buffer_size >= 8)
    {
        uint8_t * buffer = (uint8_t *)request->buffer;
        DRIVER_DEBUG("virtio_blk: read block=%i sector=%i driver_buffer_first_bytes=%i,%i,%i,%i,%i,%i,%i,%i\n",
                     (int)request->block, (int)virtio_blk_header.sector, (int)buffer[0], (int)buffer[1], (int)buffer[2],
                     (int)buffer[3], (int)buffer[4], (int)buffer[5], (int)buffer[6], (int)buffer[7]);
    }

    DRIVER_DEBUG("virtio_blk: block=%i completed virtio_status=%i used=%i last=%i\n", (int)request->block,
                 (int)virtio_blk_status, (int)used->idx, (int)self->last_used_idx);
    return virtio_blk_status == VIRTIO_BLK_S_OK ? SERVICE_OK : SERVICE_BLOCK_REQUEST_FAILED;
}

static BlockProtocolStatus VirtioBlk__transfer_channel(BlockServerContext context, BlockProtocolOperation operation,
                                                       BlockProtocolBlockNumber block, BlockProtocolBlockCount count,
                                                       ConstBufferDescriptorPtr buffer)
{
    VirtioBlkDevicePtr self = context;
    if (count == 0 || count > buffer->capacity / BLOCK_CHANNEL_SECTOR_SIZE ||
        (operation != BLOCK_PROTOCOL_READ && operation != BLOCK_PROTOCOL_WRITE))
    {
        return BLOCK_PROTOCOL_INVALID_ARGUMENT;
    }
    for (BlockProtocolBlockCount completed = 0; completed < count;)
    {
        BlockProtocolBlockCount batch = count - completed;
        uint64_t offset = completed * BLOCK_CHANNEL_SECTOR_SIZE;
        int64_t address = BufferMemory__operation(MEM_BUFFER_DEVICE_ADDRESS, buffer->id, offset,
                                                  batch * BLOCK_CHANNEL_SECTOR_SIZE, buffer->direction);
        // No size/segment-limit features are negotiated. Split only when the
        // backing is not contiguous enough for a single data descriptor.
        while (address == BUFFER_INVALID && batch > 1)
        {
            batch /= 2;
            address = BufferMemory__operation(MEM_BUFFER_DEVICE_ADDRESS, buffer->id, offset,
                                              batch * BLOCK_CHANNEL_SECTOR_SIZE, buffer->direction);
        }
        if (address < 0)
        {
            return BLOCK_PROTOCOL_IO_ERROR;
        }
        VirtioBlkTransfer request =
            VirtioBlkTransfer__new(operation, block + completed, batch, buffer->address + offset);
        if (!service_ok(VirtioBlk__execute_dma(self, &request, address)))
        {
            return BLOCK_PROTOCOL_IO_ERROR;
        }
        completed += batch;
    }
    return BLOCK_PROTOCOL_OK;
}

int VirtioBlk__run(ConstBStringPtr slot_argument, ConstBStringPtr path)
{
    uint64_t slot;
    match_optional(BString__parse_uint64(slot_argument),
        some(value)
        {
            slot = value;
        },
        none()
        {
            return 1;
        }
    );

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("virtio_blk: service registration failed\n");
        return 1;
    }

    VirtioBlkDevice device;
    match_result(
        VirtioBlk__bind_device(slot, path),
        success(value)
        {
            device = value;
        },
        error(code, message)
        {
            return 1;
        }
    );

    if (!service_ok(service_claim_virtio_device((Address)device.mmio)))
    {
        DRIVER_DEBUG("virtio_blk: claim failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }
    VirtioBlk__acknowledge_driver(device.mmio);
    if (!VirtioBlk__setup_queue(&device))
    {
        DRIVER_DEBUG("virtio_blk: queue setup failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }

    if (!service_ok(service_register_block_device(&device.path, 512, device.block_count)))
    {
        DRIVER_DEBUG("virtio_blk: block device registration failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }

    BlockServer block_server;
    if (BlockServer__listen_into(&block_server, &device.path, device.block_count, &device,
                                 VirtioBlk__transfer_channel) != CHANNEL_OK)
    {
        DRIVER_DEBUG("virtio_blk: block server listen failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }

    if (!service_ok(service_publish_ready(&device.path)))
    {
        DRIVER_DEBUG("virtio_blk: ready publish failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }

    if (!service_ok(VirtioBlkChannelIO__subscribe(device.irq_id)))
    {
        DRIVER_DEBUG("virtio_blk: irq subscribe failed path=%s\n", BString__cstr(&device.path));
        return 1;
    }

    return VirtioBlkRuntime__run(&device, &block_server);
}

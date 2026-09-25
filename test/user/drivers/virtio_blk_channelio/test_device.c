#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"
#include "ipc/trace_protocol.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/service.h"

#include "../../../../user/lib/sys/sync.h"

void __sync_boundary()
{
}

void __sync_cache_clean(void *, size_t)
{
}

void __sync_cache_invalidate(void *, size_t)
{
}

void __sync_cache_clean_invalidate(void *, size_t)
{
}

#define VIRTIO_MMIO_MAGIC 0x000
#define VIRTIO_MMIO_VERSION 0x004
#define VIRTIO_MMIO_DEVICE_ID 0x008
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

#define VIRTIO_MMIO_MAGIC_VALUE 0x74726976U
#define VIRTIO_DEVICE_ID_BLOCK 2U
#define VIRTIO_MMIO_FIRST_PHYS_BASE 0x0a000000ULL
#define VIRTIO_MMIO_SLOT_SIZE 0x200ULL
#define VIRTIO_MMIO_FIRST_IRQ_ID 48ULL
#define TEST_VIRTIO_SLOT_COUNT 32ULL
#define TEST_VIRTIO_REGION_SIZE (VIRTIO_MMIO_SLOT_SIZE * TEST_VIRTIO_SLOT_COUNT)
#define TEST_SERVICE_DEVICE_PATH_MAX 256ULL

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

typedef struct VirtioBlkDevice VirtioBlkDevice;
typedef VirtioBlkDevice * VirtioBlkDevicePtr;

typedef struct FakeVirtioSlot
{
    uint32_t magic;
    uint32_t version;
    uint32_t device_id;
    uint32_t vendor_id;
    uint32_t queue_num_max;
    uint32_t guest_page_size;
    uint32_t queue_sel;
    uint32_t queue_num;
    uint32_t queue_align;
    uint32_t queue_pfn;
    uint32_t queue_notify;
    uint32_t interrupt_status;
    uint32_t interrupt_ack;
    uint64_t capacity;
    uint32_t status_writes[5];
    uint64_t status_write_count;
} FakeVirtioSlot, *FakeVirtioSlotPtr;

static FakeVirtioSlot fake_slots[TEST_VIRTIO_SLOT_COUNT];
static uint8_t fake_virtio_region[TEST_VIRTIO_REGION_SIZE];
static ServiceMappedMemory fake_virtio_mapping;
static uint64_t fake_map_count;
static uint64_t fake_service_wait_count;
static uint64_t fake_expected_irqs[TEST_VIRTIO_SLOT_COUNT];
static uint64_t fake_expected_irq_count;
static uint64_t fake_registered_paths_count;
static char fake_registered_paths[TEST_VIRTIO_SLOT_COUNT][TEST_SERVICE_DEVICE_PATH_MAX];
static uint64_t fake_registered_block_counts[TEST_VIRTIO_SLOT_COUNT];
static bool fake_became_service;
static bool fake_irq_subscribed;
static bool fake_block_registered;
static bool fake_logged;
static const char * fake_log_tag;
static bool fake_block_request_mode;
static bool fake_block_request_fetched;
static bool fake_block_completed;
static bool fake_ready_published;
static BString fake_ready_name;
static uint8_t fake_request_buffer[512];
static uint8_t fake_second_request_buffer[512];
static ServiceCode fake_completion_status;
static uint64_t fake_completion_blocks;
static uint32_t fake_submitted_type;
static uint64_t fake_submitted_sector;
static bool fake_active_device_ready;
static VirtioBlkDevicePtr fake_active_device;
static bool fake_direct_dma;
static uint8_t fake_shared_buffer[2 * 1024 * 1024];
static uint64_t fake_dma_limit;
static unsigned fake_dma_submissions;
static bool fake_dma_denied;

#include "../../../../user/drivers/virtio_blk_channelio/device.c"

static FakeVirtioSlotPtr fake_slot_for_base(void * base)
{
    uint64_t region_base = (uint64_t)fake_virtio_region;
    uint64_t address = (uint64_t)base;
    if (address >= region_base && address < region_base + TEST_VIRTIO_REGION_SIZE)
    {
        uint64_t offset = address - region_base;
        if (offset % VIRTIO_MMIO_SLOT_SIZE == 0)
        {
            uint64_t slot = offset / VIRTIO_MMIO_SLOT_SIZE;
            return &fake_slots[slot];
        }
    }

    TEST_FAIL_MESSAGE("unexpected virtio base");
    return &fake_slots[0];
}

uint32_t VirtioBlkMmio__read32(void * base, uint64_t offset)
{
    TEST_ASSERT_NOT_NULL(base);
    FakeVirtioSlotPtr slot = fake_slot_for_base(base);
    if (offset == VIRTIO_MMIO_MAGIC)
    {
        return slot->magic;
    }
    if (offset == VIRTIO_MMIO_VERSION)
    {
        return slot->version;
    }
    if (offset == VIRTIO_MMIO_DEVICE_ID)
    {
        return slot->device_id;
    }
    if (offset == VIRTIO_MMIO_VENDOR_ID)
    {
        return slot->vendor_id;
    }
    if (offset == VIRTIO_MMIO_QUEUE_NUM_MAX)
    {
        return slot->queue_num_max;
    }
    if (offset == VIRTIO_MMIO_INTERRUPT_STAT)
    {
        return slot->interrupt_status;
    }
    if (offset == VIRTIO_MMIO_CONFIG)
    {
        return (uint32_t)(slot->capacity & 0xffffffffU);
    }
    if (offset == VIRTIO_MMIO_CONFIG + 4)
    {
        return (uint32_t)(slot->capacity >> 32);
    }

    TEST_FAIL_MESSAGE("unexpected virtio read offset");
    return 0;
}

void VirtioBlkMmio__write32(void * base, uint64_t offset, uint32_t value)
{
    TEST_ASSERT_NOT_NULL(base);
    FakeVirtioSlotPtr slot = fake_slot_for_base(base);
    if (offset == VIRTIO_MMIO_STATUS)
    {
        TEST_ASSERT_LESS_THAN_UINT64(5, slot->status_write_count);
        slot->status_writes[slot->status_write_count++] = value;
        return;
    }
    if (offset == VIRTIO_MMIO_GUEST_PAGE_SIZE)
    {
        slot->guest_page_size = value;
        return;
    }
    if (offset == VIRTIO_MMIO_QUEUE_SEL)
    {
        slot->queue_sel = value;
        return;
    }
    if (offset == VIRTIO_MMIO_QUEUE_NUM)
    {
        slot->queue_num = value;
        return;
    }
    if (offset == VIRTIO_MMIO_QUEUE_ALIGN)
    {
        slot->queue_align = value;
        return;
    }
    if (offset == VIRTIO_MMIO_QUEUE_PFN)
    {
        slot->queue_pfn = value;
        return;
    }
    if (offset == VIRTIO_MMIO_QUEUE_NOTIFY)
    {
        slot->queue_notify = value;
        if (fake_direct_dma)
        {
            VirtqDescPtr descriptors = VirtioBlk__descs(fake_active_device);
            TEST_ASSERT_GREATER_OR_EQUAL_UINT64((Address)fake_shared_buffer, descriptors[1].addr);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64((Address)fake_shared_buffer + sizeof(fake_shared_buffer),
                                             descriptors[1].addr + descriptors[1].len);
            TEST_ASSERT_LESS_OR_EQUAL_UINT64(fake_dma_limit, descriptors[1].len);
            if (virtio_blk_header.type == VIRTIO_BLK_T_IN)
            {
                __builtin_memset((void *)descriptors[1].addr, 0x5a, descriptors[1].len);
            }
            else
            {
                TEST_ASSERT_EACH_EQUAL_UINT8(0xa5, (void *)descriptors[1].addr, descriptors[1].len);
                TEST_ASSERT_EQUAL_UINT16(VIRTQ_DESC_F_NEXT, descriptors[1].flags);
            }
            virtio_blk_status = VIRTIO_BLK_S_OK;
            VirtioBlk__used(fake_active_device)->idx++;
            fake_dma_submissions++;
        }
        return;
    }
    if (offset == VIRTIO_MMIO_INTERRUPT_ACK)
    {
        slot->interrupt_ack = value;
        return;
    }

    TEST_FAIL_MESSAGE("unexpected virtio write offset");
}

void setUp(void)
{
    for (uint64_t slot = 0; slot < TEST_VIRTIO_SLOT_COUNT; slot++)
    {
        fake_slots[slot] = (FakeVirtioSlot){.magic = VIRTIO_MMIO_MAGIC_VALUE,
                                            .version = 1,
                                            .device_id = 0,
                                            .vendor_id = 0x554d4551U,
                                            .queue_num_max = 8,
                                            .interrupt_status = 3,
                                            .interrupt_ack = 0,
                                            .capacity = 1024,
                                            .status_write_count = 0};
        for (uint64_t i = 0; i < 5; i++)
        {
            fake_slots[slot].status_writes[i] = 0;
        }
    }
    fake_slots[0].device_id = VIRTIO_DEVICE_ID_BLOCK;
    fake_virtio_mapping = (ServiceMappedMemory){.driver_address = fake_virtio_region,
                                                .device_address = VIRTIO_MMIO_FIRST_PHYS_BASE,
                                                .size = TEST_VIRTIO_REGION_SIZE};

    fake_map_count = 0;
    fake_direct_dma = false;
    fake_dma_denied = false;
    fake_dma_limit = sizeof(fake_shared_buffer);
    fake_dma_submissions = 0;
    __builtin_memset(fake_shared_buffer, 0, sizeof(fake_shared_buffer));
    fake_service_wait_count = 0;
    fake_expected_irqs[0] = VIRTIO_MMIO_FIRST_IRQ_ID;
    fake_expected_irq_count = 1;
    fake_registered_paths_count = 0;
    for (uint64_t i = 0; i < TEST_VIRTIO_SLOT_COUNT; i++)
    {
        fake_registered_block_counts[i] = 0;
    }
    fake_became_service = false;
    fake_irq_subscribed = false;
    fake_block_registered = false;
    fake_logged = false;
    fake_log_tag = 0;
    fake_block_request_mode = false;
    fake_block_request_fetched = false;
    fake_block_completed = false;
    fake_ready_published = false;
    fake_ready_name = empty_bstr();
    fake_completion_status = SERVICE_ABORT;
    fake_completion_blocks = 0;
    fake_submitted_type = 0xffffffffU;
    fake_submitted_sector = 0xffffffffffffffffULL;
    fake_active_device_ready = false;
    for (uint64_t i = 0; i < sizeof(fake_request_buffer); i++)
    {
        fake_request_buffer[i] = 0;
        fake_second_request_buffer[i] = 0;
    }
}

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    fake_logged = true;
    fake_log_tag = tag;
    return TRACE_OK;
}

void tearDown(void)
{
}

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(VIRTIO_MMIO_FIRST_PHYS_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(TEST_VIRTIO_REGION_SIZE, size);
    fake_map_count++;
    *memory = &fake_virtio_mapping;
    return SERVICE_OK;
}

Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    BootHardwareInfo info = BootHardwareInfo__new();
    BootVirtioMmioInfo virtio_mmio = BootVirtioMmioInfo__new(VIRTIO_MMIO_FIRST_PHYS_BASE, VIRTIO_MMIO_SLOT_SIZE,
                                                             TEST_VIRTIO_SLOT_COUNT, VIRTIO_MMIO_FIRST_IRQ_ID);
    info.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    return Success(BootHardwareInfo, info);
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->driver_address;
}

uint64_t device_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->device_address;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    TEST_ASSERT_LESS_THAN_UINT64(memory->size, offset);
    return (void *)((uint64_t)memory->driver_address + offset);
}

uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return memory->device_address + offset;
}

size_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}

ServiceCode VirtioBlkChannelIO__subscribe(uint64_t irq)
{
    bool matched = false;
    for (uint64_t i = 0; i < fake_expected_irq_count; i++)
    {
        if (fake_expected_irqs[i] == irq)
        {
            matched = true;
            break;
        }
    }
    TEST_ASSERT_TRUE(matched);
    fake_irq_subscribed = true;
    return SERVICE_OK;
}

ServiceCode service_claim_virtio_device(Address mapping)
{
    return SERVICE_OK;
}

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    TEST_ASSERT_EQUAL_UINT64(512, block_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_VIRTIO_SLOT_COUNT, fake_registered_paths_count);
    BString__copy_to(path, fake_registered_paths[fake_registered_paths_count], TEST_SERVICE_DEVICE_PATH_MAX);
    fake_registered_block_counts[fake_registered_paths_count] = block_count;
    fake_registered_paths_count++;
    fake_block_registered = true;
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    fake_ready_name = BString__slice(name, 0, BString__length(name));
    fake_ready_published = true;
    return SERVICE_OK;
}

ServiceCode service_device_address(void * driver_address, AddressPtr device_address)
{
    TEST_ASSERT_NOT_NULL(driver_address);
    TEST_ASSERT_NOT_NULL(device_address);
    *device_address = (uint64_t)driver_address;
    return SERVICE_OK;
}

int64_t BufferMemory__operation(MemOperation operation, uint64_t first, uint64_t second, uint64_t third,
                                uint64_t fourth)
{
    TEST_ASSERT_EQUAL(MEM_BUFFER_DEVICE_ADDRESS, operation);
    TEST_ASSERT_EQUAL_UINT64(BUFFER_LIMIT, first);
    TEST_ASSERT_TRUE(fourth == BUFFER_READ || fourth == BUFFER_WRITE);
    if (fake_dma_denied)
    {
        return BUFFER_DENIED;
    }
    if (third > fake_dma_limit || second > sizeof(fake_shared_buffer) || third > sizeof(fake_shared_buffer) - second)
    {
        return BUFFER_INVALID;
    }
    return (Address)fake_shared_buffer + second;
}

Result(BlockServer) BlockServer__listen(ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                        BlockServerContext context, BlockServerTransfer transfer)
{
    return Success(BlockServer, (BlockServer){0});
}

ChannelCode BlockServer__listen_into(BlockServerPtr self, ConstBStringPtr name, BlockProtocolBlockCount block_count,
                                     BlockServerContext context, BlockServerTransfer transfer)
{
    *self = (BlockServer){0};
    return CHANNEL_OK;
}

bool BlockServer__poll(BlockServerPtr self)
{
    return false;
}

void Channel__prepare_wait(void)
{
}

void Channel__wait_event(void)
{
}

ServiceCode service_poll(ServiceEventPtr event)
{
    return service_wait(event);
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;

    if (fake_service_wait_count == 1)
    {
        event->type = SERVICE_EVENT_IRQ;
        event->source = fake_expected_irqs[0];
        event->data = 0;
        return SERVICE_OK;
    }

    return SERVICE_COMPLETE;
}

ServiceCode VirtioBlkChannelIO__acknowledge(IRQNumber irq)
{
    return SERVICE_OK;
}

ServiceCode VirtioBlkRuntime__await_irq(ServiceEventPtr event)
{
    return service_wait(event);
}

// A synchronous test runtime drives the real hardware implementation with fake events.
int VirtioBlkRuntime__run(VirtioBlkDevicePtr device, BlockServerPtr server)
{
    static VirtioBlkDevice saved_device;
    saved_device = *device;
    fake_active_device = &saved_device;
    fake_active_device_ready = true;
    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
        if (event.type == SERVICE_EVENT_IRQ && event.source == device->irq_id)
        {
            VirtioBlk__ack_interrupt(device->mmio);
        }
        saved_device = *device;
    }
    return 0;
}

static int virtio_blk_main_under_test(int argc, char ** argv)
{
    BString slot = BString__new(argv[1]);
    BString path = BString__new(argv[2]);
    return VirtioBlk__run(&slot, &path);
}

void test_virtio_blk_main_maps_first_mmio_slot_and_acknowledges_irq(void)
{
    char * argv[] = {"virtio_blk_channelio", "0", "/dev/virtio1"};
    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(1, fake_registered_paths_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", fake_registered_paths[0]);
    TEST_ASSERT_EQUAL_BSTRING(fake_ready_name, bstr("/dev/virtio1"));
    TEST_ASSERT_EQUAL_UINT64(1024, fake_registered_block_counts[0]);
    TEST_ASSERT_EQUAL_UINT64(1, fake_map_count);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(4, fake_slots[0].status_write_count);
    TEST_ASSERT_EQUAL_UINT32(0, fake_slots[0].status_writes[0]);
    TEST_ASSERT_EQUAL_UINT32(1, fake_slots[0].status_writes[1]);
    TEST_ASSERT_EQUAL_UINT32(3, fake_slots[0].status_writes[2]);
    TEST_ASSERT_EQUAL_UINT32(7, fake_slots[0].status_writes[3]);
    TEST_ASSERT_EQUAL_UINT32(4096, fake_slots[0].guest_page_size);
    TEST_ASSERT_EQUAL_UINT32(0, fake_slots[0].queue_sel);
    TEST_ASSERT_EQUAL_UINT32(8, fake_slots[0].queue_num);
    TEST_ASSERT_EQUAL_UINT32(4096, fake_slots[0].queue_align);
    TEST_ASSERT_NOT_EQUAL(0, fake_slots[0].queue_pfn);
    TEST_ASSERT_EQUAL_UINT32(fake_slots[0].interrupt_status, fake_slots[0].interrupt_ack);
}

void test_virtio_blk_main_scans_to_second_mmio_slot_and_uses_matching_irq(void)
{
    fake_slots[1].device_id = VIRTIO_DEVICE_ID_BLOCK;
    fake_expected_irqs[0] = VIRTIO_MMIO_FIRST_IRQ_ID + 1;

    char * argv[] = {"virtio_blk_channelio", "1", "/dev/virtio2"};
    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(1, fake_registered_paths_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", fake_registered_paths[0]);
    TEST_ASSERT_EQUAL_UINT64(1024, fake_registered_block_counts[0]);
    TEST_ASSERT_EQUAL_UINT64(1, fake_map_count);
    TEST_ASSERT_EQUAL_UINT64(4, fake_slots[1].status_write_count);
    TEST_ASSERT_EQUAL_UINT32(0, fake_slots[1].status_writes[0]);
    TEST_ASSERT_EQUAL_UINT32(1, fake_slots[1].status_writes[1]);
    TEST_ASSERT_EQUAL_UINT32(3, fake_slots[1].status_writes[2]);
    TEST_ASSERT_EQUAL_UINT32(7, fake_slots[1].status_writes[3]);
    TEST_ASSERT_EQUAL_UINT32(fake_slots[1].interrupt_status, fake_slots[1].interrupt_ack);
}

void test_virtio_blk_main_exits_when_no_mmio_slot_is_block_device(void)
{
    fake_slots[0].device_id = 0;

    char * argv[] = {"virtio_blk_channelio", "0", "/dev/virtio1"};
    TEST_ASSERT_EQUAL_INT(1, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_FALSE(fake_irq_subscribed);
    TEST_ASSERT_FALSE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_logged);
    TEST_ASSERT_EQUAL_STRING("VIRTIO-BLK", fake_log_tag);
    TEST_ASSERT_EQUAL_UINT64(1, fake_map_count);
    TEST_ASSERT_EQUAL_UINT64(0, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(0, fake_slots[0].status_write_count);
}

void test_virtio_blk_main_limits_runtime_to_first_discovered_block_device(void)
{
    fake_slots[1].device_id = VIRTIO_DEVICE_ID_BLOCK;
    fake_slots[1].capacity = 2048;
    fake_expected_irqs[0] = VIRTIO_MMIO_FIRST_IRQ_ID;
    fake_expected_irq_count = 1;
    char * argv[] = {"virtio_blk_channelio", "0", "/dev/virtio1"};

    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(1, fake_registered_paths_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", fake_registered_paths[0]);
    TEST_ASSERT_EQUAL_UINT64(1024, fake_registered_block_counts[0]);
    TEST_ASSERT_EQUAL_UINT64(4, fake_slots[0].status_write_count);
    TEST_ASSERT_EQUAL_UINT64(0, fake_slots[1].status_write_count);
}

static BufferDescriptor prepare_direct_dma(void)
{
    char * argv[] = {"virtio_blk_channelio", "0", "/dev/virtio1"};
    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));
    fake_direct_dma = true;
    BufferDescriptor buffer = BufferDescriptor__new(BUFFER_LIMIT, BUFFER_READ);
    buffer.address = (Address)fake_shared_buffer;
    return buffer;
}

void test_virtio_reads_full_shared_buffer_in_one_hardware_submission(void)
{
    BufferDescriptor buffer = prepare_direct_dma();
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK, VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 0,
                                                                     BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE, &buffer));
    TEST_ASSERT_EQUAL_UINT(1, fake_dma_submissions);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x5a, fake_shared_buffer, sizeof(fake_shared_buffer));
}

void test_virtio_splits_noncontiguous_shared_ranges_without_staging(void)
{
    BufferDescriptor buffer = prepare_direct_dma();
    fake_dma_limit = 4096;
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 3, 16, &buffer));
    TEST_ASSERT_EQUAL_UINT(2, fake_dma_submissions);
    TEST_ASSERT_EACH_EQUAL_UINT8(0x5a, fake_shared_buffer, 8192);
    TEST_ASSERT_EQUAL_UINT64(11, virtio_blk_header.sector);
    TEST_ASSERT_EQUAL_UINT8(0, fake_shared_buffer[8192]);
}

void test_virtio_writes_from_the_shared_buffer_without_staging(void)
{
    BufferDescriptor buffer = prepare_direct_dma();
    buffer.direction = BUFFER_WRITE;
    __builtin_memset(fake_shared_buffer, 0xa5, sizeof(fake_shared_buffer));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_OK,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_WRITE, 0, 16, &buffer));
    TEST_ASSERT_EQUAL_UINT(1, fake_dma_submissions);
    TEST_ASSERT_EQUAL_UINT32(VIRTIO_BLK_T_OUT, virtio_blk_header.type);
}

void test_virtio_rejects_denied_or_oversized_shared_transfers_without_submission(void)
{
    BufferDescriptor buffer = prepare_direct_dma();
    fake_dma_denied = true;
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 0, 1, &buffer));
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_INVALID_ARGUMENT,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 0,
                                                  BUFFER_SIZE / BLOCK_CHANNEL_SECTOR_SIZE + 1, &buffer));
    TEST_ASSERT_EQUAL_UINT(0, fake_dma_submissions);
}

void test_virtio_does_not_reuse_queue_after_uncertain_completion(void)
{
    BufferDescriptor buffer = prepare_direct_dma();
    fake_direct_dma = false;
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 0, 1, &buffer));
    TEST_ASSERT_TRUE(fake_active_device->io_failed);
    uint16_t submitted = VirtioBlk__avail(fake_active_device)->idx;
    fake_direct_dma = true;
    TEST_ASSERT_EQUAL(BLOCK_PROTOCOL_IO_ERROR,
                      VirtioBlk__transfer_channel(fake_active_device, BLOCK_PROTOCOL_READ, 1, 1, &buffer));
    TEST_ASSERT_EQUAL_UINT16(submitted, VirtioBlk__avail(fake_active_device)->idx);
    TEST_ASSERT_EQUAL_UINT(0, fake_dma_submissions);
}

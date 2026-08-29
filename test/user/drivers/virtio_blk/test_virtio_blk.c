#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common/types/bstring.h"
#include "../../../../common_includes/syscalls/service.h"

#include "../../../../user/sys/sync.h"

void __sync_boundary()
{

}

#define VIRTIO_MMIO_MAGIC          0x000
#define VIRTIO_MMIO_VERSION        0x004
#define VIRTIO_MMIO_DEVICE_ID      0x008
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
#define VIRTIO_MMIO_FIRST_PHYS_BASE 0x0a000000ULL
#define VIRTIO_MMIO_SLOT_SIZE       0x200ULL
#define VIRTIO_MMIO_FIRST_IRQ_ID    48ULL
#define TEST_VIRTIO_SLOT_COUNT      32ULL
#define TEST_VIRTIO_REGION_SIZE     (VIRTIO_MMIO_SLOT_SIZE * TEST_VIRTIO_SLOT_COUNT)
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
} FakeVirtioSlot, * FakeVirtioSlotPtr;

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
static ServiceBlockRequest fake_block_request;
static ServiceBlockRequest fake_second_block_request;
static ServiceCode fake_completion_status;
static uint64_t fake_completion_blocks;
static uint32_t fake_submitted_type;
static uint64_t fake_submitted_sector;
static bool fake_active_device_ready;
static VirtioBlkDevicePtr fake_active_device;

static uint32_t fake_virtio_read32(void * base, uint64_t offset);
static void fake_virtio_write32(void * base, uint64_t offset, uint32_t value);

#define VIRTIO_BLK_ENTRYPOINT virtio_blk_main_under_test
#define VIRTIO_READ32 fake_virtio_read32
#define VIRTIO_WRITE32 fake_virtio_write32
#define VIRTIO_BLK_TEST_DEVICE_READY(device) do { fake_active_device = (device); fake_active_device_ready = true; } while (false)
#include "../../../../user/drivers/virtio_blk/main.c"


static FakeVirtioSlotPtr fake_slot_for_base(void * base)
{
    uint64_t region_base = (uint64_t) fake_virtio_region;
    uint64_t address = (uint64_t) base;
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

static uint32_t fake_virtio_read32(void * base, uint64_t offset)
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
        return (uint32_t) (slot->capacity & 0xffffffffU);
    }
    if (offset == VIRTIO_MMIO_CONFIG + 4)
    {
        return (uint32_t) (slot->capacity >> 32);
    }

    TEST_FAIL_MESSAGE("unexpected virtio read offset");
    return 0;
}

static void fake_virtio_write32(void * base, uint64_t offset, uint32_t value)
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
        fake_slots[slot] = (FakeVirtioSlot) {
            .magic = VIRTIO_MMIO_MAGIC_VALUE,
            .version = 1,
            .device_id = 0,
            .vendor_id = 0x554d4551U,
            .queue_num_max = 8,
            .interrupt_status = 3,
            .interrupt_ack = 0,
            .capacity = 1024,
            .status_write_count = 0
        };
        for (uint64_t i = 0; i < 5; i++)
        {
            fake_slots[slot].status_writes[i] = 0;
        }
    }
    fake_slots[0].device_id = VIRTIO_DEVICE_ID_BLOCK;
    fake_virtio_mapping = (ServiceMappedMemory) {
        .driver_address = fake_virtio_region,
        .device_address = VIRTIO_MMIO_FIRST_PHYS_BASE,
        .size = TEST_VIRTIO_REGION_SIZE
    };

    fake_map_count = 0;
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
    fake_block_request = (ServiceBlockRequest) {
        .request_id = 40,
        .device_path = bstr("/dev/virtio1"),
        .operation = SERVICE_BLOCK_READ,
        .block = 3,
        .block_count = 1,
        .block_size = 512,
        .buffer = (Address) fake_request_buffer,
        .buffer_size = sizeof(fake_request_buffer),
    };
    fake_second_block_request = (ServiceBlockRequest) {
        .request_id = 41,
        .device_path = bstr("/dev/virtio1"),
        .operation = SERVICE_BLOCK_READ,
        .block = 4,
        .block_count = 1,
        .block_size = 512,
        .buffer = (Address) fake_second_request_buffer,
        .buffer_size = sizeof(fake_second_request_buffer),
    };
    for (uint64_t i = 0; i < sizeof(fake_request_buffer); i++)
    {
        fake_request_buffer[i] = 0;
        fake_second_request_buffer[i] = 0;
    }
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    fake_logged = true;
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    fake_logged = true;
    fake_log_tag = tag;
    return SYSLOG_OK;
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
    return (void *) ((uint64_t) memory->driver_address + offset);
}

uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return memory->device_address + offset;
}

size_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}

ServiceCode service_subscribe_irq(uint64_t irq)
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

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    TEST_ASSERT_EQUAL_UINT64(512, block_size);
    TEST_ASSERT_LESS_THAN_UINT64(TEST_VIRTIO_SLOT_COUNT, fake_registered_paths_count);
    BString__copy_to(
        path,
        fake_registered_paths[fake_registered_paths_count],
        TEST_SERVICE_DEVICE_PATH_MAX
    );
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

ServiceCode service_block_get_request(uint64_t request_id, ServiceBlockRequestPtr request)
{
    if (!fake_block_request_mode)
    {
        TEST_FAIL_MESSAGE("virtio block request handling is not exercised by this test");
        return SERVICE_BLOCK_REQUEST_NOT_FOUND;
    }

    TEST_ASSERT_EQUAL_UINT64(fake_block_request.request_id, request_id);
    *request = fake_block_request;
    fake_block_request_fetched = true;
    return SERVICE_OK;
}

ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count)
{
    if (!fake_block_request_mode)
    {
        TEST_FAIL_MESSAGE("virtio block request completion is not exercised by this test");
        return SERVICE_BLOCK_REQUEST_FAILED;
    }

    TEST_ASSERT_EQUAL_UINT64(fake_block_request.request_id, request_id);
    fake_block_completed = true;
    fake_completion_status = status;
    fake_completion_blocks = block_count;
    return SERVICE_OK;
}

ServiceCode service_device_address(void * driver_address, Address * device_address)
{
    TEST_ASSERT_NOT_NULL(driver_address);
    TEST_ASSERT_NOT_NULL(device_address);
    *device_address = (uint64_t) driver_address;
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;
    if (fake_block_request_mode)
    {
        if (fake_service_wait_count == 1)
        {
            event->type = SERVICE_EVENT_BLOCK_IO;
            event->source = fake_block_request.request_id;
            event->data = 0;
            return SERVICE_OK;
        }
        if (fake_service_wait_count == 2)
        {
            TEST_ASSERT_TRUE(fake_active_device_ready);
            fake_submitted_type = virtio_blk_header.type;
            fake_submitted_sector = virtio_blk_header.sector;
            virtio_blk_status = 0;
            VirtioBlk__used(fake_active_device)->ring[VirtioBlk__used(fake_active_device)->idx % VIRTIO_BLK_QUEUE_SIZE] = (VirtqUsedElem) {
                .id = 0,
                .len = 512,
            };
            VirtioBlk__used(fake_active_device)->idx++;
            event->type = SERVICE_EVENT_IRQ;
            event->source = fake_expected_irqs[0];
            event->data = 0;
            return SERVICE_OK;
        }

        return SERVICE_COMPLETE;
    }

    if (fake_service_wait_count == 1)
    {
        event->type = SERVICE_EVENT_IRQ;
        event->source = fake_expected_irqs[0];
        event->data = 0;
        return SERVICE_OK;
    }

    return SERVICE_COMPLETE;
}

void test_virtio_blk_main_maps_first_mmio_slot_and_acknowledges_irq(void)
{
    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };
    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(1, fake_registered_paths_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", fake_registered_paths[0]);
    BString expected_ready_name = bstr("/dev/virtio1");
    TEST_ASSERT_TRUE(BString__equals(&fake_ready_name, &expected_ready_name));
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

    char * argv[] = { "virtio_blk", "1", "/dev/virtio2" };
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

    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };
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
    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };

    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(1, fake_registered_paths_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", fake_registered_paths[0]);
    TEST_ASSERT_EQUAL_UINT64(1024, fake_registered_block_counts[0]);
    TEST_ASSERT_EQUAL_UINT64(4, fake_slots[0].status_write_count);
    TEST_ASSERT_EQUAL_UINT64(0, fake_slots[1].status_write_count);
}

void test_virtio_blk_main_completes_block_read_request_through_queue(void)
{
    fake_block_request_mode = true;
    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };

    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_block_request_fetched);
    TEST_ASSERT_TRUE(fake_block_completed);
    TEST_ASSERT_EQUAL(SERVICE_OK, fake_completion_status);
    TEST_ASSERT_EQUAL_UINT64(1, fake_completion_blocks);
    TEST_ASSERT_EQUAL_UINT32(0, fake_slots[0].queue_notify);

    VirtqDesc * descs = VirtioBlk__descs(fake_active_device);
    TEST_ASSERT_EQUAL_UINT32(sizeof(VirtioBlkOuthdr), descs[0].len);
    TEST_ASSERT_EQUAL_UINT16(1, descs[0].flags);
    TEST_ASSERT_EQUAL_UINT16(1, descs[0].next);
    TEST_ASSERT_EQUAL_UINT32(512, descs[1].len);
    TEST_ASSERT_EQUAL_UINT16(3, descs[1].flags);
    TEST_ASSERT_EQUAL_UINT16(2, descs[1].next);
    TEST_ASSERT_EQUAL_UINT32(1, descs[2].len);
    TEST_ASSERT_EQUAL_UINT16(2, descs[2].flags);

    TEST_ASSERT_EQUAL_UINT32(0, fake_submitted_type);
    TEST_ASSERT_EQUAL_UINT64(3, fake_submitted_sector);
    TEST_ASSERT_EQUAL_UINT16(1, VirtioBlk__avail(fake_active_device)->idx);
    TEST_ASSERT_EQUAL_UINT16(0, VirtioBlk__avail(fake_active_device)->ring[0]);
}

void test_virtio_blk_main_completes_block_write_request_through_queue(void)
{
    fake_block_request_mode = true;
    fake_block_request.operation = SERVICE_BLOCK_WRITE;
    fake_block_request.block = 4;
    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };

    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_block_request_fetched);
    TEST_ASSERT_TRUE(fake_block_completed);
    TEST_ASSERT_EQUAL(SERVICE_OK, fake_completion_status);
    TEST_ASSERT_EQUAL_UINT64(1, fake_completion_blocks);

    VirtqDesc * descs = VirtioBlk__descs(fake_active_device);
    TEST_ASSERT_EQUAL_UINT32(512, descs[1].len);
    TEST_ASSERT_EQUAL_UINT16(1, descs[1].flags);
    TEST_ASSERT_EQUAL_UINT16(2, descs[1].next);
    TEST_ASSERT_EQUAL_UINT32(1, fake_submitted_type);
    TEST_ASSERT_EQUAL_UINT64(4, fake_submitted_sector);
}

void test_virtio_blk_main_delivers_out_of_order_used_entries_in_request_order(void)
{
    fake_block_request_mode = true;
    const char * path = "/dev/virtio2";
    fake_block_request.device_path = BString__new(path);
    char * argv[] = { "virtio_blk", "0", "/dev/virtio1" };

    TEST_ASSERT_EQUAL_INT(0, virtio_blk_main_under_test(3, argv));

    TEST_ASSERT_TRUE(fake_block_request_fetched);
    TEST_ASSERT_TRUE(fake_block_completed);
    TEST_ASSERT_EQUAL(SERVICE_BLOCK_REQUEST_FAILED, fake_completion_status);
    TEST_ASSERT_EQUAL_UINT64(0, fake_completion_blocks);
    TEST_ASSERT_EQUAL_UINT16(0, VirtioBlk__avail(fake_active_device)->idx);
}

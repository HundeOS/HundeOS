#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/service.h"
#include "../../../../user/lib/sys/pci_config.h"
#include "../../../../user/lib/sys/sd_host_config.h"

void __sync_boundary()
{
}

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

typedef struct FakeSdhciHost
{
    uint8_t power_control;
    uint8_t software_reset;
    uint8_t timeout_control;
    uint16_t clock_control;
    uint16_t block_size;
    uint16_t block_count;
    uint16_t transfer_mode;
    uint16_t command;
    uint32_t argument;
    uint32_t present_state;
    uint32_t int_status;
    uint32_t int_enable;
    uint32_t signal_enable;
    uint32_t capabilities;
    uint32_t host_version;
    uint32_t response[4];
    uint64_t command_count;
    uint64_t read_words_remaining;
    uint64_t read_word_index;
    bool read_active;
} FakeSdhciHost;

static uint32_t fake_sdhci_read32(void * base, uint64_t offset);
static void fake_sdhci_write32(void * base, uint64_t offset, uint32_t value);
static uint16_t fake_sdhci_read16(void * base, uint64_t offset);
static void fake_sdhci_write16(void * base, uint64_t offset, uint16_t value);
static uint8_t fake_sdhci_read8(void * base, uint64_t offset);
static void fake_sdhci_write8(void * base, uint64_t offset, uint8_t value);
static Optional(SdHostConfig) fake_sdhci_load_config(void);
static bool fake_sdhci_load_pci_config(PciConfigPtr config);

#define SDHCI_ENTRYPOINT sdhci_main_under_test
#define SDHCI_READ32 fake_sdhci_read32
#define SDHCI_WRITE32 fake_sdhci_write32
#define SDHCI_READ16 fake_sdhci_read16
#define SDHCI_WRITE16 fake_sdhci_write16
#define SDHCI_READ8 fake_sdhci_read8
#define SDHCI_WRITE8 fake_sdhci_write8
#define SDHCI_LOAD_CONFIG fake_sdhci_load_config
#define SDHCI_LOAD_PCI_CONFIG fake_sdhci_load_pci_config
#include "../../../../user/drivers/sdhci/main.c"

#define FAKE_SD_HOST_PHYSICAL_BASE 0xfe340000ULL
#define FAKE_SD_HOST_MMIO_SIZE 0x1000ULL
#define FAKE_SD_HOST_IRQ 126ULL
#define FAKE_PCI_ECAM_PHYSICAL_BASE 0x4010000000ULL
#define FAKE_PCI_ECAM_SIZE 0x100000ULL
#define FAKE_PCI_SD_HOST_PHYSICAL_BASE 0x4020000000ULL
#define FAKE_PCI_SD_HOST_IRQ 42ULL
#define TEST_SERVICE_DEVICE_PATH_MAX 256ULL

static FakeSdhciHost fake_host;
static ServiceMappedMemory fake_mapping;
static ServiceMappedMemory fake_ecam_mapping;
static uint8_t fake_pci_ecam[FAKE_PCI_ECAM_SIZE];
static bool fake_use_pci_config;
static uint64_t fake_expected_host_physical_base;
static uint64_t fake_expected_host_mmio_size;
static uint64_t fake_expected_host_irq;
static bool fake_became_service;
static bool fake_block_registered;
static bool fake_ready_published;
static bool fake_irq_subscribed;
static bool fake_block_request_fetched;
static bool fake_block_completed;
static bool fake_logged;
static const char * fake_log_tag;
static char fake_registered_path[TEST_SERVICE_DEVICE_PATH_MAX];
static BString fake_ready_name;
static uint64_t fake_registered_block_size;
static uint64_t fake_registered_block_count;
static uint64_t fake_subscribed_irq;
static uint64_t fake_service_wait_count;
static uint8_t fake_request_buffer[512];
static ServiceBlockRequest fake_block_request;
static ServiceCode fake_completion_status;
static uint64_t fake_completion_blocks;

static void fake_write_pci8(uint64_t offset, uint8_t value)
{
    fake_pci_ecam[offset] = value;
}

static void fake_write_pci16(uint64_t offset, uint16_t value)
{
    fake_pci_ecam[offset] = (uint8_t)value;
    fake_pci_ecam[offset + 1] = (uint8_t)(value >> 8);
}

static void fake_write_pci32(uint64_t offset, uint32_t value)
{
    fake_pci_ecam[offset] = (uint8_t)value;
    fake_pci_ecam[offset + 1] = (uint8_t)(value >> 8);
    fake_pci_ecam[offset + 2] = (uint8_t)(value >> 16);
    fake_pci_ecam[offset + 3] = (uint8_t)(value >> 24);
}

static void fake_add_pci_sdhci_function(uint64_t device)
{
    uint64_t offset = device * SDHCI_PCI_ECAM_DEVICE_SIZE;
    fake_write_pci16(offset + SDHCI_PCI_CONFIG_VENDOR_ID, 0x1234U);
    fake_write_pci8(offset + SDHCI_PCI_CONFIG_CLASS_CODE, SDHCI_PCI_CLASS_SYSTEM_PERIPHERAL);
    fake_write_pci8(offset + SDHCI_PCI_CONFIG_SUBCLASS, SDHCI_PCI_SUBCLASS_SD_HOST);
    fake_write_pci8(offset + SDHCI_PCI_CONFIG_HEADER_TYPE, 0);
    fake_write_pci32(offset + SDHCI_PCI_CONFIG_BAR0,
                     ((uint32_t)FAKE_PCI_SD_HOST_PHYSICAL_BASE) | SDHCI_PCI_BAR_MEMORY_64);
    fake_write_pci32(offset + SDHCI_PCI_CONFIG_BAR0 + 4, (uint32_t)(FAKE_PCI_SD_HOST_PHYSICAL_BASE >> 32));
    fake_write_pci8(offset + SDHCI_PCI_CONFIG_INTERRUPT_LINE, FAKE_PCI_SD_HOST_IRQ);
}

static void fake_set_response_for_command(uint32_t command_index)
{
    fake_host.response[0] = 0;
    fake_host.response[1] = 0;
    fake_host.response[2] = 0;
    fake_host.response[3] = 0;

    if (command_index == SD_CMD_SEND_IF_COND)
    {
        fake_host.response[0] = 0x1aa;
    }
    if (command_index == SD_ACMD_SD_SEND_OP_COND)
    {
        fake_host.response[0] = SD_OCR_READY | SD_OCR_HIGH_CAPACITY;
    }
    if (command_index == SD_CMD_SEND_RELATIVE_ADDR)
    {
        fake_host.response[0] = 1U << 16;
    }
    if (command_index == SD_CMD_SEND_CSD)
    {
        fake_host.response[1] = 0x00010000U;
        fake_host.response[3] = 0x40000000U;
    }
}

static void fake_start_read_command(void)
{
    fake_host.read_active = true;
    fake_host.read_word_index = 0;
    fake_host.read_words_remaining = ((uint64_t)fake_host.block_count) * (SDHCI_BLOCK_SIZE / sizeof(uint32_t));
}

static uint32_t fake_sdhci_read32(void * base, uint64_t offset)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_PRESENT_STATE)
    {
        return fake_host.present_state;
    }
    if (offset == SDHCI_INT_STATUS)
    {
        return fake_host.int_status;
    }
    if (offset == SDHCI_CAPABILITIES)
    {
        return fake_host.capabilities;
    }
    if (offset == SDHCI_HOST_VERSION)
    {
        return fake_host.host_version;
    }
    if (offset >= SDHCI_RESPONSE && offset < SDHCI_RESPONSE + 16)
    {
        return fake_host.response[(offset - SDHCI_RESPONSE) / sizeof(uint32_t)];
    }
    if (offset == SDHCI_BUFFER)
    {
        TEST_ASSERT_TRUE(fake_host.read_active);
        TEST_ASSERT_GREATER_THAN_UINT64(0, fake_host.read_words_remaining);
        uint32_t value = 0x03020100U + (uint32_t)(fake_host.read_word_index * 0x04040404U);
        fake_host.read_word_index++;
        fake_host.read_words_remaining--;
        if (fake_host.read_words_remaining == 0)
        {
            fake_host.read_active = false;
            fake_host.int_status |= SDHCI_INT_TRANSFER_COMPLETE;
        }
        return value;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci read32 offset");
    return 0;
}

static void fake_sdhci_write32(void * base, uint64_t offset, uint32_t value)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_INT_STATUS)
    {
        fake_host.int_status &= ~value;
        if (fake_host.read_active && (value & SDHCI_INT_COMMAND_COMPLETE) != 0)
        {
            fake_host.int_status |= SDHCI_INT_BUFFER_READ_READY;
        }
        return;
    }
    if (offset == SDHCI_INT_ENABLE)
    {
        fake_host.int_enable = value;
        return;
    }
    if (offset == SDHCI_SIGNAL_ENABLE)
    {
        fake_host.signal_enable = value;
        return;
    }
    if (offset == SDHCI_ARGUMENT)
    {
        fake_host.argument = value;
        return;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci write32 offset");
}

static uint16_t fake_sdhci_read16(void * base, uint64_t offset)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_CLOCK_CONTROL)
    {
        if ((fake_host.clock_control & SDHCI_CLOCK_INTERNAL_ENABLE) != 0)
        {
            return fake_host.clock_control | SDHCI_CLOCK_INTERNAL_STABLE;
        }
        return fake_host.clock_control;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci read16 offset");
    return 0;
}

static void fake_sdhci_write16(void * base, uint64_t offset, uint16_t value)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_CLOCK_CONTROL)
    {
        fake_host.clock_control = value;
        return;
    }
    if (offset == SDHCI_BLOCK_SIZE_REG)
    {
        fake_host.block_size = value;
        return;
    }
    if (offset == SDHCI_BLOCK_COUNT_REG)
    {
        fake_host.block_count = value;
        return;
    }
    if (offset == SDHCI_TRANSFER_MODE)
    {
        fake_host.transfer_mode = value;
        return;
    }
    if (offset == SDHCI_COMMAND)
    {
        fake_host.command = value;
        fake_host.command_count++;
        uint32_t command_index = value >> 8;
        fake_set_response_for_command(command_index);
        if (command_index == SD_CMD_READ_SINGLE_BLOCK || command_index == SD_CMD_READ_MULTIPLE_BLOCK)
        {
            fake_start_read_command();
        }
        fake_host.int_status |= SDHCI_INT_COMMAND_COMPLETE;
        return;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci write16 offset");
}

static uint8_t fake_sdhci_read8(void * base, uint64_t offset)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_SOFTWARE_RESET)
    {
        return 0;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci read8 offset");
    return 0;
}

static void fake_sdhci_write8(void * base, uint64_t offset, uint8_t value)
{
    TEST_ASSERT_EQUAL_PTR(&fake_host, base);
    if (offset == SDHCI_SOFTWARE_RESET)
    {
        fake_host.software_reset = value;
        return;
    }
    if (offset == SDHCI_TIMEOUT_CONTROL)
    {
        fake_host.timeout_control = value;
        return;
    }
    if (offset == SDHCI_POWER_CONTROL)
    {
        fake_host.power_control = value;
        return;
    }

    TEST_FAIL_MESSAGE("unexpected sdhci write8 offset");
}

static Optional(SdHostConfig) fake_sdhci_load_config(void)
{
    return Some(SdHostConfig, ((SdHostConfig){
                                  .physical_base = FAKE_SD_HOST_PHYSICAL_BASE,
                                  .mmio_size = FAKE_SD_HOST_MMIO_SIZE,
                                  .irq = FAKE_SD_HOST_IRQ,
                              }));
}

static bool fake_sdhci_load_pci_config(PciConfigPtr config)
{
    if (!fake_use_pci_config)
    {
        return false;
    }

    *config = (PciConfig){
        .ecam_physical_base = FAKE_PCI_ECAM_PHYSICAL_BASE,
        .ecam_size = FAKE_PCI_ECAM_SIZE,
        .bus_start = 0,
        .bus_end = 0,
    };

    return true;
}

void setUp(void)
{
    fake_host = (FakeSdhciHost){
        .present_state = SDHCI_PRESENT_CARD_INSERTED,
        .capabilities = 100U << 8,
        .host_version = 0x00001002U,
    };
    fake_mapping = (ServiceMappedMemory){
        .driver_address = &fake_host,
        .device_address = FAKE_SD_HOST_PHYSICAL_BASE,
        .size = FAKE_SD_HOST_MMIO_SIZE,
    };
    fake_ecam_mapping = (ServiceMappedMemory){
        .driver_address = fake_pci_ecam,
        .device_address = FAKE_PCI_ECAM_PHYSICAL_BASE,
        .size = FAKE_PCI_ECAM_SIZE,
    };
    for (uint64_t index = 0; index < sizeof(fake_pci_ecam); index++)
    {
        fake_pci_ecam[index] = 0xffU;
    }
    fake_use_pci_config = false;
    fake_expected_host_physical_base = FAKE_SD_HOST_PHYSICAL_BASE;
    fake_expected_host_mmio_size = FAKE_SD_HOST_MMIO_SIZE;
    fake_expected_host_irq = FAKE_SD_HOST_IRQ;
    fake_became_service = false;
    fake_block_registered = false;
    fake_ready_published = false;
    fake_irq_subscribed = false;
    fake_block_request_fetched = false;
    fake_block_completed = false;
    fake_logged = false;
    fake_log_tag = 0;
    fake_registered_path[0] = '\0';
    fake_registered_block_size = 0;
    fake_registered_block_count = 0;
    fake_subscribed_irq = 0;
    fake_service_wait_count = 0;
    fake_completion_status = SERVICE_ABORT;
    fake_completion_blocks = 0;
    for (uint64_t index = 0; index < sizeof(fake_request_buffer); index++)
    {
        fake_request_buffer[index] = 0;
    }
    fake_block_request = (ServiceBlockRequest){
        .request_id = 77,
        .operation = SERVICE_BLOCK_READ,
        .block = 3,
        .block_count = 1,
        .block_size = SDHCI_BLOCK_SIZE,
        .buffer = (Address)fake_request_buffer,
        .buffer_size = sizeof(fake_request_buffer),
        .device_path = bstr(SDHCI_DEVICE_PATH),
    };
    fake_ready_name = empty_bstr();
}

void tearDown(void)
{
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

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    if (physical_address == FAKE_PCI_ECAM_PHYSICAL_BASE)
    {
        TEST_ASSERT_EQUAL_UINT64(FAKE_PCI_ECAM_SIZE, size);
        *memory = &fake_ecam_mapping;
        return SERVICE_OK;
    }

    TEST_ASSERT_EQUAL_UINT64(fake_expected_host_physical_base, physical_address);
    TEST_ASSERT_EQUAL_UINT64(fake_expected_host_mmio_size, size);
    *memory = &fake_mapping;
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

ServiceCode service_register_block_device(ConstBStringPtr path, uint64_t block_size, uint64_t block_count)
{
    BString__copy_to(path, fake_registered_path, TEST_SERVICE_DEVICE_PATH_MAX);
    fake_registered_block_size = block_size;
    fake_registered_block_count = block_count;
    fake_block_registered = true;
    return SERVICE_OK;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    fake_subscribed_irq = irq;
    fake_irq_subscribed = true;
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
    TEST_ASSERT_EQUAL_UINT64(fake_block_request.request_id, request_id);
    *request = fake_block_request;
    fake_block_request_fetched = true;
    return SERVICE_OK;
}

ServiceCode service_block_complete(uint64_t request_id, ServiceCode status, uint64_t block_count)
{
    TEST_ASSERT_EQUAL_UINT64(fake_block_request.request_id, request_id);
    fake_completion_status = status;
    fake_completion_blocks = block_count;
    fake_block_completed = true;
    return SERVICE_OK;
}

ServiceCode service_device_address(void * driver_address, Address * device_address)
{
    *device_address = (uint64_t)driver_address;
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;
    if (fake_service_wait_count == 1)
    {
        event->type = SERVICE_EVENT_BLOCK_IO;
        event->source = fake_block_request.request_id;
        event->data = 0;
        return SERVICE_OK;
    }

    return SERVICE_COMPLETE;
}

void test_sdhci_main_registers_mmc0_and_completes_read_request(void)
{
    TEST_ASSERT_EQUAL_INT(0, sdhci_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_TRUE(fake_block_request_fetched);
    TEST_ASSERT_TRUE(fake_block_completed);
    TEST_ASSERT_EQUAL_STRING(SDHCI_DEVICE_PATH, fake_registered_path);
    TEST_ASSERT_EQUAL_STRING(SDHCI_DEVICE_PATH, BString__cstr(&fake_ready_name));
    TEST_ASSERT_EQUAL_UINT64(SDHCI_BLOCK_SIZE, fake_registered_block_size);
    TEST_ASSERT_EQUAL_UINT64(2048, fake_registered_block_count);
    TEST_ASSERT_EQUAL_UINT64(FAKE_SD_HOST_IRQ, fake_subscribed_irq);
    TEST_ASSERT_EQUAL(SERVICE_OK, fake_completion_status);
    TEST_ASSERT_EQUAL_UINT64(1, fake_completion_blocks);
    TEST_ASSERT_EQUAL_UINT8(0, fake_request_buffer[0]);
    TEST_ASSERT_EQUAL_UINT8(1, fake_request_buffer[1]);
    TEST_ASSERT_EQUAL_UINT8(2, fake_request_buffer[2]);
    TEST_ASSERT_EQUAL_UINT8(3, fake_request_buffer[3]);
    TEST_ASSERT_EQUAL_UINT32(3, fake_host.argument);
    TEST_ASSERT_TRUE(fake_logged);
    TEST_ASSERT_EQUAL_STRING("SDHCI", fake_log_tag);
}

void test_sdhci_main_prefers_pci_sdhci_host(void)
{
    fake_use_pci_config = true;
    fake_expected_host_physical_base = FAKE_PCI_SD_HOST_PHYSICAL_BASE;
    fake_expected_host_mmio_size = SDHCI_PCI_MMIO_SIZE;
    fake_expected_host_irq = FAKE_PCI_SD_HOST_IRQ;
    fake_add_pci_sdhci_function(3);

    TEST_ASSERT_EQUAL_INT(0, sdhci_main_under_test());

    TEST_ASSERT_TRUE(fake_block_registered);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_TRUE(fake_irq_subscribed);
    TEST_ASSERT_EQUAL_UINT64(fake_expected_host_irq, fake_subscribed_irq);
}

void test_sdhci_card_block_count_from_v2_csd(void)
{
    SdhciCommandResult csd = {
        .ok = true,
        .response = {0, 0x00030000U, 0, 0x40000000U},
    };

    TEST_ASSERT_EQUAL_UINT64(4096, Sdhci__card_block_count_from_csd(&csd));
}

#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define UART_DR   0x000
#define UART_FR   0x018
#define UART_CR   0x030
#define UART_IMSC 0x038
#define UART_ICR  0x044
#define FR_RXFE   (1u << 4)
#define UART_PHYS_BASE 0x12345000ULL
#define UART_IRQ_ID 99ULL

static uint32_t fake_uart_values[4];
static uint64_t fake_uart_read_index;
static uint64_t fake_uart_read_count;
static uint32_t fake_uart_icr_writes[2];
static uint64_t fake_uart_icr_write_count;
static uint32_t fake_uart_imsc;
static uint32_t fake_uart_cr;
static char fake_io_buffer[8];
static bool fake_data_available;
static bool fake_became_service;
static uint64_t fake_service_wait_count;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;

#define SYSINFO_PADDING_LINE "memory.free_region.00.physical_start=0x0\n"
#define SYSINFO_PADDING_4 SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE
#define SYSINFO_PADDING_16 SYSINFO_PADDING_4 SYSINFO_PADDING_4 SYSINFO_PADDING_4 SYSINFO_PADDING_4
#define SYSINFO_PADDING_32 SYSINFO_PADDING_16 SYSINFO_PADDING_16

static const char fake_sysinfo[] =
    SYSINFO_PADDING_32
    "uart.physical_base=0x12345000\n"
    "uart.virtual_base=0xffff000000000000\n"
    "uart.irq=99\n"
    "uart.mmio_size=0x1000\n";

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    uint64_t size;
};

static ServiceMappedMemory fake_uart_mapping;

static uint32_t fake_uart_read32(void * base, uint64_t offset);
static void fake_uart_write32(void * base, uint64_t offset, uint32_t value);

#define UART_RX_ENTRYPOINT uart_rx_main_under_test
#define UART_READ32 fake_uart_read32
#define UART_WRITE32 fake_uart_write32
#include "../../../../user/drivers/uart_rx/main.c"

static uint32_t fake_uart_read32(void * base, uint64_t offset)
{
    TEST_ASSERT_NOT_NULL(base);
    if (offset == UART_FR)
    {
        return fake_uart_read_index < 3 ? 0 : FR_RXFE;
    }
    if (offset == UART_DR)
    {
        fake_uart_read_count++;
        return fake_uart_values[fake_uart_read_index++];
    }
    TEST_FAIL_MESSAGE("unexpected uart read offset");
    return 0;
}

static void fake_uart_write32(void * base, uint64_t offset, uint32_t value)
{
    TEST_ASSERT_NOT_NULL(base);
    if (offset == UART_ICR)
    {
        TEST_ASSERT_LESS_THAN_UINT64(2, fake_uart_icr_write_count);
        fake_uart_icr_writes[fake_uart_icr_write_count++] = value;
        return;
    }
    if (offset == UART_IMSC)
    {
        fake_uart_imsc = value;
        return;
    }
    if (offset == UART_CR)
    {
        fake_uart_cr = value;
        return;
    }
    TEST_FAIL_MESSAGE("unexpected uart write offset");
}

void setUp(void)
{
    fake_uart_values[0] = 'a';
    fake_uart_values[1] = 'b';
    fake_uart_values[2] = 'c';
    fake_uart_values[3] = 0;
    fake_uart_read_index = 0;
    fake_uart_read_count = 0;
    fake_uart_icr_writes[0] = 0;
    fake_uart_icr_writes[1] = 0;
    fake_uart_icr_write_count = 0;
    fake_uart_imsc = 0;
    fake_uart_cr = 0;
    fake_uart_mapping.driver_address = fake_uart_values;
    fake_uart_mapping.device_address = UART_PHYS_BASE;
    fake_uart_mapping.size = 0x1000ULL;
    fake_data_available = false;
    fake_became_service = false;
    fake_service_wait_count = 0;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    for (uint64_t i = 0; i < sizeof(fake_io_buffer); i++)
    {
        fake_io_buffer[i] = 0;
    }
}

void tearDown(void)
{
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(UART_PHYS_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, size);
    *memory = &fake_uart_mapping;
    return SERVICE_OK;
}

IOHandle open(const char * path, Flags flags)
{
    TEST_ASSERT_EQUAL_STRING("/proc/sysinfo", path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return 7;
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    size_t index = 0;
    while (fake_sysinfo[index] != '\0' && index < size)
    {
        buf[index] = fake_sysinfo[index];
        index++;
    }
    return (ByteCount) index;
}

IOStatus close(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    fake_sysinfo_closed = true;
    return IO_OK;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, ByteCount max_bytes)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    DynString sysinfo;
    match_result(DynStringCode, DynString__init(&sysinfo, 128), success(_) {}, error(code, message) { return None(DynString); })
    for (ByteCount index = 0; fake_sysinfo[index] != '\0' && index < max_bytes; index++)
    {
        match_result(DynStringCode, DynString__push_char(&sysinfo, fake_sysinfo[index]), success(_) {}, error(code, message) {
                DynString__dispose(&sysinfo);
                return None(DynString);
            })
    }
    return Some(DynString, sysinfo);
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
    return (void *) ((uint64_t) memory->driver_address + offset);
}

uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return memory->device_address + offset;
}

uint64_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}

IOHandle io_server_register(const char * path, IOServerRole role)
{
    TEST_ASSERT_EQUAL_STRING(":tty:", path);
    TEST_ASSERT_EQUAL_INT(IO_SERVER_READ, role);
    return 42;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    TEST_ASSERT_EQUAL_UINT64(UART_IRQ_ID, irq);
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;
    TEST_ASSERT_LESS_THAN_UINT64(3, fake_service_wait_count);
    if (fake_data_available)
    {
        return SERVICE_COMPLETE;
    }

    event->type = SERVICE_EVENT_IRQ;
    event->source = UART_IRQ_ID;
    event->data = 0;
    return SERVICE_OK;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT64(42, handle);
    buffer->data = fake_io_buffer;
    buffer->capacity = sizeof(fake_io_buffer);
    return IO_OK;
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    TEST_ASSERT_EQUAL_INT64(42, handle);
    TEST_ASSERT_EQUAL_UINT64(3, byte_count);
    fake_data_available = true;
    return IO_OK;
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    TEST_FAIL_MESSAGE("rx driver should not consume write data");
    return IO_OK;
}

IOStatus io_server_eof(IOHandle handle)
{
    TEST_FAIL_MESSAGE("rx driver should not signal eof");
    return IO_OK;
}

void test_uart_rx_main_maps_registers_subscribes_irq_and_publishes_read_data(void)
{
    TEST_ASSERT_EQUAL_INT(0, uart_rx_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_data_available);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_read_count);
    TEST_ASSERT_EQUAL_CHAR('a', fake_io_buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('b', fake_io_buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('c', fake_io_buffer[2]);
    TEST_ASSERT_EQUAL_UINT64(2, fake_uart_icr_write_count);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10), fake_uart_icr_writes[0]);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6), fake_uart_imsc);
    TEST_ASSERT_EQUAL_UINT32((1u << 0) | (1u << 8) | (1u << 9), fake_uart_cr);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10), fake_uart_icr_writes[1]);
}

#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define UART_DR   0x000
#define UART_FR   0x018
#define UART_ICR  0x044
#define FR_RXFE   (1u << 4)
#define UART_IRQ_ID 33ULL

static uint32_t fake_uart_values[4];
static uint64_t fake_uart_read_index;
static uint64_t fake_uart_read_count;
static uint32_t fake_uart_icr;
static char fake_io_buffer[8];
static bool fake_data_available;
static uint64_t fake_service_wait_count;

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
    TEST_ASSERT_EQUAL_UINT64(UART_ICR, offset);
    fake_uart_icr = value;
}

void setUp(void)
{
    fake_uart_values[0] = 'a';
    fake_uart_values[1] = 'b';
    fake_uart_values[2] = 'c';
    fake_uart_values[3] = 0;
    fake_uart_read_index = 0;
    fake_uart_read_count = 0;
    fake_uart_icr = 0;
    fake_data_available = false;
    fake_service_wait_count = 0;
    for (uint64_t i = 0; i < sizeof(fake_io_buffer); i++)
    {
        fake_io_buffer[i] = 0;
    }
}

void tearDown(void)
{
}

ServiceCode service_map_mmio(ServiceMappedRangePtr range)
{
    TEST_ASSERT_EQUAL_UINT64(0x09000000ULL, range->physical);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, range->size);
    range->address = fake_uart_values;
    return SERVICE_OK;
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
    (void) handle;
    (void) byte_count;
    TEST_FAIL_MESSAGE("rx driver should not consume write data");
    return IO_OK;
}

IOStatus io_server_eof(IOHandle handle)
{
    (void) handle;
    TEST_FAIL_MESSAGE("rx driver should not signal eof");
    return IO_OK;
}

void test_uart_rx_main_maps_registers_subscribes_irq_and_publishes_read_data(void)
{
    TEST_ASSERT_EQUAL_INT(0, uart_rx_main_under_test());

    TEST_ASSERT_TRUE(fake_data_available);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_read_count);
    TEST_ASSERT_EQUAL_CHAR('a', fake_io_buffer[0]);
    TEST_ASSERT_EQUAL_CHAR('b', fake_io_buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('c', fake_io_buffer[2]);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10), fake_uart_icr);
}

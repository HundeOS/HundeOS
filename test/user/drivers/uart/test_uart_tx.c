#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define UART_DR   0x000
#define UART_FR   0x018
#define FR_TXFF   (1u << 5)

static char fake_io_buffer[2];
static uint32_t fake_uart_writes[4];
static uint64_t fake_uart_write_count;
static uint64_t fake_uart_fr_read_count;
static bool fake_data_consumed;
static uint64_t fake_service_wait_count;

static uint32_t fake_uart_read32(void * base, uint64_t offset);
static void fake_uart_write32(void * base, uint64_t offset, uint32_t value);

#define UART_TX_ENTRYPOINT uart_tx_main_under_test
#define UART_READ32 fake_uart_read32
#define UART_WRITE32 fake_uart_write32
#include "../../../../user/drivers/uart_tx/main.c"

static uint32_t fake_uart_read32(void * base, uint64_t offset)
{
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_EQUAL_UINT64(UART_FR, offset);
    fake_uart_fr_read_count++;
    return fake_uart_fr_read_count == 1 ? FR_TXFF : 0;
}

static void fake_uart_write32(void * base, uint64_t offset, uint32_t value)
{
    TEST_ASSERT_NOT_NULL(base);
    TEST_ASSERT_EQUAL_UINT64(UART_DR, offset);
    TEST_ASSERT_LESS_THAN_UINT64(4, fake_uart_write_count);
    fake_uart_writes[fake_uart_write_count++] = value;
}

void setUp(void)
{
    fake_io_buffer[0] = 'x';
    fake_io_buffer[1] = '\n';
    fake_uart_write_count = 0;
    fake_uart_fr_read_count = 0;
    fake_data_consumed = false;
    fake_service_wait_count = 0;
    for (uint64_t i = 0; i < 4; i++)
    {
        fake_uart_writes[i] = 0;
    }
}

void tearDown(void)
{
}

ServiceCode service_map_mmio(ServiceMappedRangePtr range)
{
    TEST_ASSERT_EQUAL_UINT64(0x09000000ULL, range->physical);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, range->size);
    range->address = fake_uart_writes;
    return SERVICE_OK;
}

IOHandle io_server_register(const char * path, IOServerRole role)
{
    TEST_ASSERT_EQUAL_STRING(":tty:", path);
    TEST_ASSERT_EQUAL_INT(IO_SERVER_WRITE, role);
    return 43;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    (void) irq;
    TEST_FAIL_MESSAGE("tx driver should not subscribe to an irq");
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;
    TEST_ASSERT_LESS_THAN_UINT64(3, fake_service_wait_count);
    if (fake_data_consumed)
    {
        return SERVICE_COMPLETE;
    }

    event->type = SERVICE_EVENT_IO;
    event->source = SERVICE_IO_WRITE_REQUEST;
    event->data = 0;
    return SERVICE_OK;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT64(43, handle);
    buffer->data = fake_io_buffer;
    buffer->capacity = sizeof(fake_io_buffer);
    return IO_OK;
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    (void) handle;
    (void) byte_count;
    TEST_FAIL_MESSAGE("tx driver should not publish read data");
    return IO_OK;
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    TEST_ASSERT_EQUAL_INT64(43, handle);
    TEST_ASSERT_EQUAL_UINT64(2, byte_count);
    fake_data_consumed = true;
    return IO_OK;
}

IOStatus io_server_eof(IOHandle handle)
{
    (void) handle;
    TEST_FAIL_MESSAGE("tx driver should not signal eof");
    return IO_OK;
}

void test_uart_tx_main_maps_registers_waits_for_write_request_and_consumes_data(void)
{
    TEST_ASSERT_EQUAL_INT(0, uart_tx_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_write_count);
    TEST_ASSERT_EQUAL_UINT32('x', fake_uart_writes[0]);
    TEST_ASSERT_EQUAL_UINT32('\n', fake_uart_writes[1]);
    TEST_ASSERT_EQUAL_UINT32('\r', fake_uart_writes[2]);
    TEST_ASSERT_GREATER_THAN_UINT64(3, fake_uart_fr_read_count);
}

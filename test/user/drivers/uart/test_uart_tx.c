#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define UART_DR 0x000
#define UART_FR 0x018
#define FR_TXFF (1u << 5)
#define UART_PHYS_BASE 0x12345000ULL

static char fake_io_buffer[2];
static uint32_t fake_uart_writes[4];
struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

static ServiceMappedMemory fake_uart_mapping;
static uint64_t fake_uart_write_count;
static uint64_t fake_uart_fr_read_count;
static bool fake_became_service;
static bool fake_data_consumed;
static uint64_t fake_service_wait_count;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;

static const char fake_sysinfo[] = "uart.physical_base=0x12345000\n"
                                   "uart.virtual_base=0xffff000000000000\n"
                                   "uart.irq=99\n"
                                   "uart.mmio_size=0x1000\n";

uint32_t Uart__read32(Address base, uint64_t offset);
void Uart__write32(Address base, uint64_t offset, uint32_t value);

#include "../stream_runtime.h"
#define main uart_tx_driver_main
#include "../../../../user/drivers/uart_tx/main.c"
#undef main
static int uart_tx_main_under_test(void)
{
    return StreamDriverTest__run(uart_tx_driver_main);
}

uint32_t Uart__read32(Address base, uint64_t offset)
{
    TEST_ASSERT_NOT_EQUAL(0, base);
    TEST_ASSERT_EQUAL_UINT64(UART_FR, offset);
    fake_uart_fr_read_count++;
    return fake_uart_fr_read_count == 1 ? FR_TXFF : 0;
}

void Uart__write32(Address base, uint64_t offset, uint32_t value)
{
    TEST_ASSERT_NOT_EQUAL(0, base);
    TEST_ASSERT_EQUAL_UINT64(UART_DR, offset);
    TEST_ASSERT_LESS_THAN_UINT64(4, fake_uart_write_count);
    fake_uart_writes[fake_uart_write_count++] = value;
}

void setUp(void)
{
    fake_io_buffer[0] = 'x';
    fake_io_buffer[1] = '\n';
    fake_uart_mapping.driver_address = fake_uart_writes;
    fake_uart_mapping.device_address = UART_PHYS_BASE;
    fake_uart_mapping.size = 0x1000ULL;
    fake_uart_write_count = 0;
    fake_uart_fr_read_count = 0;
    fake_became_service = false;
    fake_data_consumed = false;
    fake_service_wait_count = 0;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    for (uint64_t i = 0; i < 4; i++)
    {
        fake_uart_writes[i] = 0;
    }
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
    TEST_ASSERT_EQUAL_UINT64(UART_PHYS_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(0x1000ULL, size);
    *memory = &fake_uart_mapping;
    return SERVICE_OK;
}

IOHandle open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), *path);
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
    return (ByteCount)index;
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
    match_result(
        DynString__init(&sysinfo, 128),
        success_do_nothing(),
        error(code, message)
        {
            return None(DynString);
        }
    );
    for (ByteCount index = 0; fake_sysinfo[index] != '\0' && index < max_bytes; index++)
    {
        match_result(
            DynString__push_char(&sysinfo, fake_sysinfo[index]),
            success_do_nothing(),
            error(code, message)
            {
                DynString__dispose(&sysinfo);
                return None(DynString);
            }
        );
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

void StreamDriverTest__step(IOStreamDriverPtr driver)
{
    fake_service_wait_count++;
    if (fake_data_consumed)
    {
        StreamDriverTest__stop();
    }
    IORequest request = IORequest__new(IO_REQUEST_WRITE, 0);
    request.count = sizeof(fake_io_buffer);
    __builtin_memcpy(request.data, fake_io_buffer, request.count);
    IOReply reply = driver->transfer(driver->context, &request);
    if (reply.status == IO_WAIT_FOR_DATA)
    {
        return;
    }
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(2, reply.value);
    fake_data_consumed = true;
}

void test_uart_tx_main_maps_registers_waits_for_write_request_and_consumes_data(void)
{
    TEST_ASSERT_EQUAL_INT(0, uart_tx_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_EQUAL_UINT64(3, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_write_count);
    TEST_ASSERT_EQUAL_UINT32('x', fake_uart_writes[0]);
    TEST_ASSERT_EQUAL_UINT32('\n', fake_uart_writes[1]);
    TEST_ASSERT_EQUAL_UINT32('\r', fake_uart_writes[2]);
    TEST_ASSERT_GREATER_THAN_UINT64(3, fake_uart_fr_read_count);
}

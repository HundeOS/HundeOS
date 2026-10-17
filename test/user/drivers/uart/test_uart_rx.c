#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define UART_PHYS_BASE 0x12345000ULL
#define UART_IRQ_ID 99ULL

static uint32_t fake_uart_values[4];
static uint64_t fake_uart_read_index;
static uint64_t fake_uart_read_count;
static uint32_t fake_uart_icr_writes[2];
static uint64_t fake_uart_icr_write_count;
static uint32_t fake_uart_imsc;
static uint32_t fake_uart_cr;
static uint32_t fake_uart_interrupt_status;
static uint32_t fake_uart_device_status;
static uint64_t fake_uart_interrupt_status_count;
static char fake_io_buffer[8];
static bool fake_data_available;
static bool fake_became_service;
static uint64_t fake_service_wait_count;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static uint64_t signalled_pid;
static ProcessSignal delivered_signal;

#define SYSINFO_PADDING_LINE "memory.free_region.00.physical_start=0x0\n"
#define SYSINFO_PADDING_4 SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE SYSINFO_PADDING_LINE
#define SYSINFO_PADDING_16 SYSINFO_PADDING_4 SYSINFO_PADDING_4 SYSINFO_PADDING_4 SYSINFO_PADDING_4
#define SYSINFO_PADDING_32 SYSINFO_PADDING_16 SYSINFO_PADDING_16

static const char fake_sysinfo[] = SYSINFO_PADDING_32 "uart.physical_base=0x12345000\n"
                                                      "uart.virtual_base=0xffff000000000000\n"
                                                      "uart.irq=99\n"
                                                      "uart.kind=1\n"
                                                      "uart.mmio_size=0x1000\n"
                                                      "uart.register_shift=0\n"
                                                      "uart.register_width=4\n";

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

static ServiceMappedMemory fake_uart_mapping;

#include "../stream_runtime.h"
#define main uart_rx_driver_main
#include "../../../../user/drivers/uart_rx/main.c"
#undef main
static IODiagnostics diagnostics;

static int uart_rx_main_under_test(void)
{
    return StreamDriverTest__run(uart_rx_driver_main);
}

UartDevice UartDevice__new(Address registers, UartKind kind, uint64_t register_shift, uint64_t register_width)
{
    TEST_ASSERT_EQUAL(UART_KIND_PL011, kind);
    TEST_ASSERT_EQUAL_UINT64(0, register_shift);
    TEST_ASSERT_EQUAL_UINT64(4, register_width);
    return (UartDevice){
        .registers = registers, .kind = kind, .register_shift = register_shift, .register_width = register_width};
}

void UartDevice__configure_receiver(UartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    fake_uart_imsc = 0;
    fake_uart_icr_writes[fake_uart_icr_write_count++] =
        (1U << 4) | (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10);
    fake_uart_cr = (1U << 0) | (1U << 8) | (1U << 9);
}

void UartDevice__enable_receiver_interrupt(UartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    fake_uart_imsc = (1U << 4) | (1U << 6);
}

void UartDevice__acknowledge_receiver(UartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    fake_uart_icr_writes[fake_uart_icr_write_count++] =
        (1U << 4) | (1U << 6) | (1U << 7) | (1U << 8) | (1U << 9) | (1U << 10);
}

UartInterruptStatus UartDevice__interrupt_status(ConstUartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    fake_uart_interrupt_status_count++;
    return (UartInterruptStatus){.interrupt = fake_uart_interrupt_status, .device = fake_uart_device_status};
}

bool UartDevice__can_receive(ConstUartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    return fake_uart_read_index < 3;
}

uint8_t UartDevice__read(UartDevicePtr self)
{
    TEST_ASSERT_NOT_EQUAL(0, self->registers);
    fake_uart_read_count++;
    return fake_uart_values[fake_uart_read_index++];
}

void setUp(void)
{
    diagnostics = (IODiagnostics){0};
    IODiagnostics__attach(&diagnostics);
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
    fake_uart_interrupt_status = 0x40;
    fake_uart_device_status = 0x20;
    fake_uart_interrupt_status_count = 0;
    fake_uart_mapping.driver_address = fake_uart_values;
    fake_uart_mapping.device_address = UART_PHYS_BASE;
    fake_uart_mapping.size = 0x1000ULL;
    fake_data_available = false;
    fake_became_service = false;
    fake_service_wait_count = 0;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    signalled_pid = 0;
    delivered_signal = 0;
    for (uint64_t i = 0; i < sizeof(fake_io_buffer); i++)
    {
        fake_io_buffer[i] = 0;
    }
}

ProcessSignalResult kill_process(int64_t pid, ProcessSignal signal)
{
    signalled_pid = pid;
    delivered_signal = signal;
    return PROCESS_SIGNAL_OK;
}

void test_uart_rx_ctrl_c_signals_foreground_process_and_interrupts_pending_read(void)
{
    fake_uart_values[0] = 'a';
    fake_uart_values[1] = 0x03;
    fake_uart_values[2] = 'b';
    UartDevice uart = UartDevice__new(UART_PHYS_BASE, UART_KIND_PL011, 0, 4);
    UartReceiver receiver = UartReceiver__new(&uart);
    IORequest foreground = IORequest__new(IO_REQUEST_SET_FOREGROUND_PROCESS, 1);
    foreground.foreground_pid = 42;

    TEST_ASSERT_EQUAL(IO_OK, UartReceiver__transfer(&receiver, &foreground).status);
    TEST_ASSERT_TRUE(UartReceiver__drain(&receiver));

    TEST_ASSERT_EQUAL_UINT64(42, signalled_pid);
    TEST_ASSERT_EQUAL_INT(PROCESS_SIGNAL_INTERRUPT, delivered_signal);
    TEST_ASSERT_EQUAL_UINT64(2, UartInput__len(&receiver.input));
    IORequest read_request = IORequest__new(IO_REQUEST_READ, 1);
    read_request.client_pid = 42;
    read_request.count = 1;
    TEST_ASSERT_EQUAL(IO_INTERRUPTED, UartReceiver__transfer(&receiver, &read_request).status);
    TEST_ASSERT_EQUAL(IO_OK, UartReceiver__transfer(&receiver, &read_request).status);
}

void tearDown(void)
{
}

void test_uart_rx_retains_fifo_bytes_when_userspace_buffer_is_full(void)
{
    UartDevice uart = UartDevice__new(UART_PHYS_BASE, UART_KIND_PL011, 0, 4);
    UartReceiver receiver = UartReceiver__new(&uart);
    for (uint64_t i = 0; i < UartInput__max_size; i++)
    {
        match_result(UartInput__push(&receiver.input, 'x'),
            success_do_nothing(),
            error_test_fail()
        );
    }
    TEST_ASSERT_FALSE(UartReceiver__drain(&receiver));
    TEST_ASSERT_EQUAL_UINT64(0, fake_uart_read_count);
    IORequest request = {.operation = IO_REQUEST_READ, .count = 2};
    IOReply reply = UartReceiver__transfer(&receiver, &request);
    TEST_ASSERT_EQUAL_INT64(2, reply.value);
    TEST_ASSERT_TRUE(UartReceiver__drain(&receiver));
    TEST_ASSERT_EQUAL_UINT64(2, fake_uart_read_count);
    TEST_ASSERT_EQUAL_UINT64(UartInput__max_size, UartInput__len(&receiver.input));
}

void test_uart_rx_empty_read_waits_but_zero_length_read_completes(void)
{
    UartDevice uart = UartDevice__new(UART_PHYS_BASE, UART_KIND_PL011, 0, 4);
    UartReceiver receiver = UartReceiver__new(&uart);
    IORequest request = {.operation = IO_REQUEST_READ, .count = 1};
    IOReply reply = UartReceiver__transfer(&receiver, &request);
    TEST_ASSERT_EQUAL_INT(IO_WAIT_FOR_DATA, reply.status);
    request.count = 0;
    reply = UartReceiver__transfer(&receiver, &request);
    TEST_ASSERT_EQUAL_INT(IO_OK, reply.status);
    TEST_ASSERT_EQUAL_INT64(0, reply.value);
    IODiagnosticsSnapshot totals = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_EQUAL_UINT64(2, totals.uart_reads);
    TEST_ASSERT_EQUAL_UINT64(1, totals.uart_empty);
    TEST_ASSERT_EQUAL_UINT64(0, totals.uart_bytes);
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

IOHandle IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), *path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return 7;
}

ByteCount IO__read_bytes(IOHandle handle, char * buf, size_t size)
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

IOStatus IO__close(IOHandle handle)
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

static bool irq_delivered;

ServiceCode InterruptClient__subscribe(IRQNumber irq)
{
    TEST_ASSERT_EQUAL_UINT64(UART_IRQ_ID, irq);
    irq_delivered = false;
    return SERVICE_OK;
}

ServiceCode InterruptClient__poll(ServiceEventPtr event)
{
    if (irq_delivered)
    {
        return SERVICE_EVENT_QUEUE_EMPTY;
    }
    irq_delivered = true;
    *event = ServiceEvent__new(SERVICE_EVENT_IRQ, UART_IRQ_ID, 1);
    return SERVICE_OK;
}

ServiceCode InterruptClient__acknowledge(IRQNumber irq)
{
    TEST_ASSERT_EQUAL_UINT64(UART_IRQ_ID, irq);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_read_count);
    TEST_ASSERT_EQUAL_UINT64(2, fake_uart_icr_write_count);
    return SERVICE_OK;
}

void StreamDriverTest__step(IOStreamDriverPtr driver)
{
    fake_service_wait_count++;
    if (fake_data_available)
    {
        StreamDriverTest__stop();
    }
    IORequest request = IORequest__new(IO_REQUEST_READ, 0);
    request.count = sizeof(fake_io_buffer);
    IOReply reply = driver->transfer(driver->context, &request);
    if (reply.status == IO_WAIT_FOR_DATA)
    {
        return;
    }
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(3, reply.value);
    __builtin_memcpy(fake_io_buffer, reply.data, reply.value);
    fake_data_available = true;
}

void test_uart_rx_main_maps_registers_subscribes_irq_and_publishes_read_data(void)
{
    TEST_ASSERT_EQUAL_INT(0, uart_rx_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_data_available);
    TEST_ASSERT_EQUAL_UINT64(3, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT64(3, fake_uart_read_count);
    TEST_ASSERT_EQUAL_CHAR('a', fake_io_buffer[0]);
    IODiagnosticsSnapshot totals = IODiagnostics__snapshot(&diagnostics);
    TEST_ASSERT_EQUAL_UINT64(3, totals.uart_bytes);
    TEST_ASSERT_EQUAL_UINT64(1, totals.uart_irqs);
    TEST_ASSERT_EQUAL_CHAR('b', fake_io_buffer[1]);
    TEST_ASSERT_EQUAL_CHAR('c', fake_io_buffer[2]);
    TEST_ASSERT_EQUAL_UINT64(2, fake_uart_icr_write_count);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10),
                             fake_uart_icr_writes[0]);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6), fake_uart_imsc);
    TEST_ASSERT_EQUAL_UINT32((1u << 0) | (1u << 8) | (1u << 9), fake_uart_cr);
    TEST_ASSERT_EQUAL_UINT32((1u << 4) | (1u << 6) | (1u << 7) | (1u << 8) | (1u << 9) | (1u << 10),
                             fake_uart_icr_writes[1]);
}

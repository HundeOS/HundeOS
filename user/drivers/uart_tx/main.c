#include "io/stream_driver.h"
#include "sys/service.h"
#include "sys/syslog.h"
#include "sys/uart_config.h"
#include "devices/uart.h"

typedef struct UartTransmitter
{
    UartDevice uart;
} UartTransmitter;

typedef UartTransmitter * UartTransmitterPtr;

static UartTransmitter UartTransmitter__new(ConstUartDevicePtr uart)
{
    return (UartTransmitter){.uart = *uart};
}

static void UartTransmitter__write_byte(UartTransmitterPtr self, uint8_t value)
{
    while (!UartDevice__can_transmit(&self->uart))
    {
    }
    UartDevice__write(&self->uart, value);
}

static IOReply UartTransmitter__write(IOServiceContext context, IORequestPtr request)
{
    UartTransmitterPtr self = context;
    IOReply reply = IOReply__new(IO_OK, 0);
    while ((uint64_t)reply.value < request->count)
    {
        uint8_t value = IORequest__write_source(request)[reply.value++];
        UartTransmitter__write_byte(self, value);
        if (value == '\n')
        {
            UartTransmitter__write_byte(self, '\r');
        }
    }
    return reply;
}

int main(void)
{
    if (!service_ok(service_become_service()))
    {
        kernel_syslog_tagf("uart_tx", "startup failed: become service\n");
        return 1;
    }
    UartDriverConfig config;
    if (!UartDriverConfig__load(&config))
    {
        kernel_syslog_tagf("uart_tx", "startup failed: load UART config\n");
        return 1;
    }
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &mapping)))
    {
        kernel_syslog_tagf("uart_tx", "startup failed: map MMIO\n");
        return 1;
    }
    static UartTransmitter transmitter;
    UartDevice uart =
        UartDevice__new((Address)driver_ptr(mapping), config.kind, config.register_shift, config.register_width);
    transmitter = UartTransmitter__new(&uart);
    static IOStreamDriver driver;
    static IOService service;
    driver = IOStreamDriver__new_terminal(IO_OPEN_WRITE, &transmitter, UartTransmitter__write);
    if (IOService__listen_into(&service, &bstr(IO_UART_TX_SERVICE), &driver, IOStreamDriver__execute,
                               IOStreamDriver__disconnect) != IO_OK)
    {
        kernel_syslog_tagf("uart_tx", "startup failed: listen\n");
        return 1;
    }
    ServiceCode ready_status = service_publish_ready(&bstr(IO_UART_TX_SERVICE));
    if (!service_ok(ready_status))
    {
        kernel_syslog_tagf("uart_tx", "startup failed: publish ready status=%i\n", (int)ready_status);
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        bool progress = IOService__poll(&service);
        if (!progress)
        {
            Channel__wait_event();
        }
    }
}

#include "io/stream_driver.h"
#include "sys/service.h"
#include "sys/uart_config.h"
#include "devices/uart.h"
#define UART_DR 0x000
#define UART_FR 0x018
#define FR_TXFF (1u << 5)

typedef struct UartTransmitter
{
    Address registers;
    bool pending_carriage_return;
} UartTransmitter;
typedef UartTransmitter * UartTransmitterPtr;
static UartTransmitter UartTransmitter__new(Address registers)
{
    return (UartTransmitter){.registers = registers};
}

static bool UartTransmitter__flush(UartTransmitterPtr self)
{
    if (!self->pending_carriage_return || (Uart__read32(self->registers, UART_FR) & FR_TXFF) != 0)
    {
        return false;
    }
    Uart__write32(self->registers, UART_DR, '\r');
    self->pending_carriage_return = false;
    return true;
}

static IOReply UartTransmitter__write(IOServiceContext context, IORequestPtr request)
{
    UartTransmitterPtr self = context;
    IOReply reply = IOReply__new(IO_OK, 0);
    UartTransmitter__flush(self);
    while (!self->pending_carriage_return && (uint64_t)reply.value < request->count &&
           (Uart__read32(self->registers, UART_FR) & FR_TXFF) == 0)
    {
        uint8_t value = IORequest__write_source(request)[reply.value++];
        Uart__write32(self->registers, UART_DR, value);
        if (value == '\n')
        {
            self->pending_carriage_return = true;
            UartTransmitter__flush(self);
        }
    }
    if (reply.value == 0 && request->count != 0)
    {
        reply.status = IO_WAIT_FOR_DATA;
    }
    return reply;
}

int main(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    UartDriverConfig config;
    if (!UartDriverConfig__load(&config))
    {
        return 1;
    }
    ServiceMappedMemoryPtr mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &mapping)))
    {
        return 1;
    }
    static UartTransmitter transmitter;
    transmitter = UartTransmitter__new((Address)driver_ptr(mapping));
    static IOStreamDriver driver;
    static IOService service;
    driver = IOStreamDriver__new(IO_OPEN_WRITE, &transmitter, UartTransmitter__write);
    if (IOService__listen_into(&service, &bstr(IO_UART_TX_SERVICE), &driver, IOStreamDriver__execute,
                               IOStreamDriver__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(IO_UART_TX_SERVICE))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        bool progress = IOService__poll(&service);
        progress = UartTransmitter__flush(&transmitter) || progress;
        if (!progress)
        {
            Channel__wait_event();
        }
    }
}

#include "io/stream_driver.h"
#include "ipc/io_diagnostics.h"
#include "interrupt/client.h"
#include "sys/uart_config.h"
#include "devices/uart.h"

#define UART_DR 0x000
#define UART_FR 0x018
#define UART_CR 0x030
#define UART_IMSC 0x038
#define UART_ICR 0x044
#define FR_RXFE (1u << 4)
#define UART_RX_INTERRUPTS ((1u << 4) | (1u << 6))
#define UART_ERRORS ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 10))

static bool UartByte__equals(void * first, void * second)
{
    return *(uint8_t *)first == *(uint8_t *)second;
}
OptionalDef(uint8_t);
FixedArrayDefine(uint8_t, 1024, UartInput, UartByte__equals);
FixedArrayBodyDefine(UartInput);

typedef struct UartReceiver
{
    Address registers;
    UartInput input;
} UartReceiver;
typedef UartReceiver * UartReceiverPtr;
static UartReceiver UartReceiver__new(Address registers)
{
    return (UartReceiver){.registers = registers, .input = UartInput__new()};
}

static bool UartReceiver__drain(UartReceiverPtr self)
{
    bool progress = false;
    while (UartInput__len(&self->input) < UartInput__max_size &&
           (Uart__read32(self->registers, UART_FR) & FR_RXFE) == 0)
    {
        uint8_t value = Uart__read32(self->registers, UART_DR);
        IODiagnostics__record(IO_DIAGNOSTIC_UART_BYTES, 1);
        match_result(
            UartInput__push(&self->input, value),
            success(code)
            {
                progress = true;
            },
            error(code, message)
            {
                return progress;
            }
        );
    }
    return progress;
}

static IOReply UartReceiver__read(IOServiceContext context, IORequestPtr request)
{
    IODiagnostics__record(IO_DIAGNOSTIC_UART_READS, 1);
    UartReceiverPtr self = context;
    IOReply reply = IOReply__new(IO_OK, 0);
    while ((uint64_t)reply.value < request->count)
    {
        match_optional(
            UartInput__pop_front(&self->input),
            some(value)
            {
                IORequest__read_target(request, &reply)[reply.value++] = value;
            },
            none()
            {
                if (reply.value == 0 && request->count != 0)
                {
                    reply.status = IO_WAIT_FOR_DATA;
                    IODiagnostics__record(IO_DIAGNOSTIC_UART_EMPTY, 1);
                }
                return reply;
            }
        );
    }
    if (reply.value == 0 && request->count != 0)
    {
        reply.status = IO_WAIT_FOR_DATA;
        IODiagnostics__record(IO_DIAGNOSTIC_UART_EMPTY, 1);
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
    static UartReceiver receiver;
    receiver = UartReceiver__new((Address)driver_ptr(mapping));
    Uart__write32(receiver.registers, UART_IMSC, 0);
    Uart__write32(receiver.registers, UART_ICR, UART_RX_INTERRUPTS | UART_ERRORS);
    Uart__write32(receiver.registers, UART_CR, (1u << 0) | (1u << 8) | (1u << 9));
    static IOStreamDriver driver;
    static IOService service;
    driver = IOStreamDriver__new(IO_OPEN_READ, &receiver, UartReceiver__read);
    if (IOService__listen_into(&service, &bstr(IO_UART_RX_SERVICE), &driver, IOStreamDriver__execute,
                               IOStreamDriver__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(InterruptClient__subscribe(config.irq)))
    {
        return 1;
    }
    Uart__write32(receiver.registers, UART_IMSC, UART_RX_INTERRUPTS);
    if (!service_ok(service_publish_ready(&bstr(IO_UART_RX_SERVICE))))
    {
        return 1;
    }
    bool pending_irq = false;
    Channel__prepare_wait();
    while (true)
    {
        bool progress = IOService__poll(&service);
        progress = UartReceiver__drain(&receiver) || progress;
        ServiceEvent event;
        ServiceCode status = InterruptClient__poll(&event);
        if (status == SERVICE_OK)
        {
            IODiagnostics__record(IO_DIAGNOSTIC_UART_IRQS, 1);
            pending_irq = true;
            progress = true;
        }
        else if (status != SERVICE_EVENT_QUEUE_EMPTY)
        {
            return 1;
        }
        // Keep the GIC source masked while the userspace buffer is full.
        if (pending_irq && (Uart__read32(receiver.registers, UART_FR) & FR_RXFE) != 0)
        {
            Uart__write32(receiver.registers, UART_ICR, UART_RX_INTERRUPTS | UART_ERRORS);
            if (!service_ok(InterruptClient__acknowledge(config.irq)))
            {
                return 1;
            }
            pending_irq = false;
            progress = true;
        }
        if (!progress)
        {
            Channel__wait_event();
        }
    }
}

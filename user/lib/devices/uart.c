#include "uart.h"

#define PL011_DATA 0x000U
#define PL011_FLAGS 0x018U
#define PL011_CONTROL 0x030U
#define PL011_INTERRUPT_MASK 0x038U
#define PL011_INTERRUPT_CLEAR 0x044U
#define PL011_RECEIVE_EMPTY (1U << 4)
#define PL011_TRANSMIT_FULL (1U << 5)
#define PL011_RECEIVE_INTERRUPTS ((1U << 4) | (1U << 6))
#define PL011_ERRORS ((1U << 7) | (1U << 8) | (1U << 9) | (1U << 10))

#define NS16550_DATA 0U
#define NS16550_INTERRUPT_ENABLE 1U
#define NS16550_LINE_STATUS 5U
#define NS16550_DATA_READY (1U << 0)
#define NS16550_TRANSMIT_EMPTY (1U << 5)
#define NS16550_RECEIVE_INTERRUPT (1U << 0)

typedef volatile uint8_t * UartRegister8Ptr;
typedef volatile uint32_t * UartRegister32Ptr;

static uint8_t UartDevice__read8(ConstUartDevicePtr self, uint64_t offset)
{
    return *(UartRegister8Ptr)(self->registers + offset);
}

static void UartDevice__write8(UartDevicePtr self, uint64_t offset, uint8_t value)
{
    *(UartRegister8Ptr)(self->registers + offset) = value;
}

static uint32_t UartDevice__read32(ConstUartDevicePtr self, uint64_t offset)
{
    return *(UartRegister32Ptr)(self->registers + offset);
}

static void UartDevice__write32(UartDevicePtr self, uint64_t offset, uint32_t value)
{
    *(UartRegister32Ptr)(self->registers + offset) = value;
}

UartDevice UartDevice__new(Address registers, UartKind kind)
{
    return (UartDevice){
        .registers = registers,
        .kind = kind,
    };
}

void UartDevice__configure_receiver(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_INTERRUPT_MASK, 0);
        UartDevice__write32(self, PL011_INTERRUPT_CLEAR, PL011_RECEIVE_INTERRUPTS | PL011_ERRORS);
        UartDevice__write32(self, PL011_CONTROL, (1U << 0) | (1U << 8) | (1U << 9));
    }
    else
    {
        UartDevice__write8(self, NS16550_INTERRUPT_ENABLE, 0);
    }
}

void UartDevice__enable_receiver_interrupt(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_INTERRUPT_MASK, PL011_RECEIVE_INTERRUPTS);
    }
    else
    {
        UartDevice__write8(self, NS16550_INTERRUPT_ENABLE, NS16550_RECEIVE_INTERRUPT);
    }
}

void UartDevice__acknowledge_receiver(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_INTERRUPT_CLEAR, PL011_RECEIVE_INTERRUPTS | PL011_ERRORS);
    }
}

bool UartDevice__can_receive(ConstUartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return (UartDevice__read32(self, PL011_FLAGS) & PL011_RECEIVE_EMPTY) == 0;
    }
    return (UartDevice__read8(self, NS16550_LINE_STATUS) & NS16550_DATA_READY) != 0;
}

uint8_t UartDevice__read(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return (uint8_t)UartDevice__read32(self, PL011_DATA);
    }
    return UartDevice__read8(self, NS16550_DATA);
}

bool UartDevice__can_transmit(ConstUartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return (UartDevice__read32(self, PL011_FLAGS) & PL011_TRANSMIT_FULL) == 0;
    }
    return (UartDevice__read8(self, NS16550_LINE_STATUS) & NS16550_TRANSMIT_EMPTY) != 0;
}

void UartDevice__write(UartDevicePtr self, uint8_t value)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_DATA, value);
    }
    else
    {
        UartDevice__write8(self, NS16550_DATA, value);
    }
}

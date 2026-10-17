#include "uart.h"

#define PL011_DATA 0x000U
#define PL011_FLAGS 0x018U
#define PL011_CONTROL 0x030U
#define PL011_INTERRUPT_MASK 0x038U
#define PL011_MASKED_INTERRUPT_STATUS 0x040U
#define PL011_INTERRUPT_CLEAR 0x044U
#define PL011_RECEIVE_EMPTY (1U << 4)
#define PL011_TRANSMIT_FULL (1U << 5)
#define PL011_RECEIVE_INTERRUPTS ((1U << 4) | (1U << 6))
#define PL011_ERRORS ((1U << 7) | (1U << 8) | (1U << 9) | (1U << 10))

#define NS16550_DATA 0U
#define NS16550_INTERRUPT_ENABLE 1U
#define NS16550_INTERRUPT_IDENTIFICATION 2U
#define NS16550_LINE_STATUS 5U
#define NS16550_DESIGNWARE_UART_STATUS 31U
#define NS16550_DATA_READY (1U << 0)
#define NS16550_TRANSMIT_EMPTY (1U << 5)
#define NS16550_RECEIVE_INTERRUPT (1U << 0)
#define NS16550_INTERRUPT_ID_MASK 0x0fU
#define NS16550_DESIGNWARE_BUSY_INTERRUPT 0x07U

typedef volatile uint8_t * UartRegister8Ptr;
typedef volatile uint32_t * UartRegister32Ptr;

static UartTransmitStatus UartTransmitStatus__new(uint32_t raw, bool ready)
{
    return (UartTransmitStatus){.raw = raw, .ready = ready};
}

static UartInterruptStatus UartInterruptStatus__new(uint32_t interrupt, uint32_t device)
{
    return (UartInterruptStatus){.interrupt = interrupt, .device = device};
}

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

static uint32_t UartDevice__read_ns16550(ConstUartDevicePtr self, uint64_t index)
{
    uint64_t offset = index << self->register_shift;
    return self->register_width == 4 ? UartDevice__read32(self, offset) : UartDevice__read8(self, offset);
}

static void UartDevice__write_ns16550(UartDevicePtr self, uint64_t index, uint8_t value)
{
    uint64_t offset = index << self->register_shift;
    if (self->register_width == 4)
    {
        UartDevice__write32(self, offset, value);
    }
    else
    {
        UartDevice__write8(self, offset, value);
    }
}

UartDevice UartDevice__new(Address registers, UartKind kind, uint64_t register_shift, uint64_t register_width)
{
    return (UartDevice){
        .registers = registers,
        .kind = kind,
        .register_shift = register_shift,
        .register_width = register_width,
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
        UartDevice__write_ns16550(self, NS16550_INTERRUPT_ENABLE, 0);
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
        UartDevice__write_ns16550(self, NS16550_INTERRUPT_ENABLE, NS16550_RECEIVE_INTERRUPT);
    }
}

void UartDevice__acknowledge_receiver(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_INTERRUPT_CLEAR, PL011_RECEIVE_INTERRUPTS | PL011_ERRORS);
    }
    else if ((UartDevice__read_ns16550(self, NS16550_INTERRUPT_IDENTIFICATION) & NS16550_INTERRUPT_ID_MASK) ==
             NS16550_DESIGNWARE_BUSY_INTERRUPT)
    {
        UartDevice__read_ns16550(self, NS16550_DESIGNWARE_UART_STATUS);
    }
}

UartInterruptStatus UartDevice__interrupt_status(ConstUartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return UartInterruptStatus__new(UartDevice__read32(self, PL011_MASKED_INTERRUPT_STATUS),
                                        UartDevice__read32(self, PL011_FLAGS));
    }
    return UartInterruptStatus__new(UartDevice__read_ns16550(self, NS16550_INTERRUPT_IDENTIFICATION),
                                    UartDevice__read_ns16550(self, NS16550_LINE_STATUS));
}

bool UartDevice__can_receive(ConstUartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return (UartDevice__read32(self, PL011_FLAGS) & PL011_RECEIVE_EMPTY) == 0;
    }
    return (UartDevice__read_ns16550(self, NS16550_LINE_STATUS) & NS16550_DATA_READY) != 0;
}

uint8_t UartDevice__read(UartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        return (uint8_t)UartDevice__read32(self, PL011_DATA);
    }
    return (uint8_t)UartDevice__read_ns16550(self, NS16550_DATA);
}

UartTransmitStatus UartDevice__transmit_status(ConstUartDevicePtr self)
{
    if (self->kind == UART_KIND_PL011)
    {
        uint32_t status = UartDevice__read32(self, PL011_FLAGS);
        return UartTransmitStatus__new(status, (status & PL011_TRANSMIT_FULL) == 0);
    }
    uint32_t status = UartDevice__read_ns16550(self, NS16550_LINE_STATUS);
    return UartTransmitStatus__new(status, (status & NS16550_TRANSMIT_EMPTY) != 0);
}

bool UartDevice__can_transmit(ConstUartDevicePtr self)
{
    return UartDevice__transmit_status(self).ready;
}

void UartDevice__write(UartDevicePtr self, uint8_t value)
{
    if (self->kind == UART_KIND_PL011)
    {
        UartDevice__write32(self, PL011_DATA, value);
    }
    else
    {
        UartDevice__write_ns16550(self, NS16550_DATA, value);
    }
}

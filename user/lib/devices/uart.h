#ifndef HUND_UART_H
#define HUND_UART_H

#include <stdbool.h>
#include <stdint.h>

#include "hardware/boot_info.h"
#include "types/address.h"

typedef struct UartDevice
{
    Address registers;
    UartKind kind;
} UartDevice;

typedef UartDevice * UartDevicePtr;
typedef const UartDevice * ConstUartDevicePtr;

UartDevice UartDevice__new(Address registers, UartKind kind);
void UartDevice__configure_receiver(UartDevicePtr self);
void UartDevice__enable_receiver_interrupt(UartDevicePtr self);
void UartDevice__acknowledge_receiver(UartDevicePtr self);
bool UartDevice__can_receive(ConstUartDevicePtr self);
uint8_t UartDevice__read(UartDevicePtr self);
bool UartDevice__can_transmit(ConstUartDevicePtr self);
void UartDevice__write(UartDevicePtr self, uint8_t value);

#endif

#ifndef HUND_UART_H
#define HUND_UART_H
#include "types/address.h"
#include <stdint.h>
uint32_t Uart__read32(Address base, uint64_t offset);
void Uart__write32(Address base, uint64_t offset, uint32_t value);
#endif

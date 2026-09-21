#include "uart.h"
typedef volatile uint32_t * UartRegisterPtr;
uint32_t Uart__read32(Address base, uint64_t offset)
{
    return *(UartRegisterPtr)(base + offset);
}
void Uart__write32(Address base, uint64_t offset, uint32_t value)
{
    *(UartRegisterPtr)(base + offset) = value;
}

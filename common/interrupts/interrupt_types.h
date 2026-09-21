#ifndef HUND_INTERRUPT_TYPES_H
#define HUND_INTERRUPT_TYPES_H

#include <stdint.h>

#define IRQ_COUNT 256U

typedef uint64_t IRQNumber;
typedef IRQNumber * IRQNumberPtr;
typedef uint64_t InterruptDeliverySequence;

#endif

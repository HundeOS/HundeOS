#include <stdint.h>

#include "unity.h"

#include "../../../../user/lib/devices/uart.c"

static uint32_t pl011_registers[32];
static uint8_t ns16550_registers[8];

void StreamDriverTest__step(void * driver)
{
}

void setUp(void)
{
    for (uint64_t index = 0; index < 32; index++)
    {
        pl011_registers[index] = 0;
    }
    for (uint64_t index = 0; index < 8; index++)
    {
        ns16550_registers[index] = 0;
    }
}

void tearDown(void)
{
}

void test_pl011_uses_word_registers_for_receive_and_transmit(void)
{
    UartDevice uart = UartDevice__new((Address)pl011_registers, UART_KIND_PL011);

    UartDevice__configure_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT32(0, pl011_registers[PL011_INTERRUPT_MASK / sizeof(uint32_t)]);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_INTERRUPTS | PL011_ERRORS,
                             pl011_registers[PL011_INTERRUPT_CLEAR / sizeof(uint32_t)]);
    TEST_ASSERT_EQUAL_UINT32((1U << 0) | (1U << 8) | (1U << 9), pl011_registers[PL011_CONTROL / sizeof(uint32_t)]);

    UartDevice__enable_receiver_interrupt(&uart);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_INTERRUPTS, pl011_registers[PL011_INTERRUPT_MASK / sizeof(uint32_t)]);
    pl011_registers[PL011_FLAGS / sizeof(uint32_t)] = 0;
    pl011_registers[PL011_DATA / sizeof(uint32_t)] = 'a';
    TEST_ASSERT_TRUE(UartDevice__can_receive(&uart));
    TEST_ASSERT_EQUAL_UINT8('a', UartDevice__read(&uart));
    TEST_ASSERT_TRUE(UartDevice__can_transmit(&uart));
    UartDevice__write(&uart, 'b');
    TEST_ASSERT_EQUAL_UINT32('b', pl011_registers[PL011_DATA / sizeof(uint32_t)]);

    pl011_registers[PL011_FLAGS / sizeof(uint32_t)] = PL011_RECEIVE_EMPTY | PL011_TRANSMIT_FULL;
    TEST_ASSERT_FALSE(UartDevice__can_receive(&uart));
    TEST_ASSERT_FALSE(UartDevice__can_transmit(&uart));
}

void test_ns16550_uses_byte_registers_for_receive_and_transmit(void)
{
    UartDevice uart = UartDevice__new((Address)ns16550_registers, UART_KIND_NS16550);

    ns16550_registers[NS16550_INTERRUPT_ENABLE] = 0xff;
    UartDevice__configure_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT8(0, ns16550_registers[NS16550_INTERRUPT_ENABLE]);
    UartDevice__enable_receiver_interrupt(&uart);
    TEST_ASSERT_EQUAL_UINT8(NS16550_RECEIVE_INTERRUPT, ns16550_registers[NS16550_INTERRUPT_ENABLE]);

    ns16550_registers[NS16550_LINE_STATUS] = NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY;
    ns16550_registers[NS16550_DATA] = 'x';
    TEST_ASSERT_TRUE(UartDevice__can_receive(&uart));
    TEST_ASSERT_EQUAL_UINT8('x', UartDevice__read(&uart));
    TEST_ASSERT_TRUE(UartDevice__can_transmit(&uart));
    UartDevice__write(&uart, 'y');
    TEST_ASSERT_EQUAL_UINT8('y', ns16550_registers[NS16550_DATA]);

    UartDevice__acknowledge_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT8(NS16550_RECEIVE_INTERRUPT, ns16550_registers[NS16550_INTERRUPT_ENABLE]);
    ns16550_registers[NS16550_LINE_STATUS] = 0;
    TEST_ASSERT_FALSE(UartDevice__can_receive(&uart));
    TEST_ASSERT_FALSE(UartDevice__can_transmit(&uart));
}

#include <stdint.h>

#include "unity.h"

#include "../../../../user/lib/devices/uart.c"

static uint32_t pl011_registers[32];
static uint8_t ns16550_registers[32];
static uint32_t ns16550_word_registers[32];

void StreamDriverTest__step(void * driver)
{
}

void setUp(void)
{
    for (uint64_t index = 0; index < 32; index++)
    {
        pl011_registers[index] = 0;
    }
    for (uint64_t index = 0; index < 32; index++)
    {
        ns16550_registers[index] = 0;
        ns16550_word_registers[index] = 0;
    }
}

void tearDown(void)
{
}

void test_pl011_uses_word_registers_for_receive_and_transmit(void)
{
    UartDevice uart = UartDevice__new((Address)pl011_registers, UART_KIND_PL011, 0, 4);

    UartDevice__configure_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT32(0, pl011_registers[PL011_INTERRUPT_MASK / sizeof(uint32_t)]);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_INTERRUPTS | PL011_ERRORS,
                             pl011_registers[PL011_INTERRUPT_CLEAR / sizeof(uint32_t)]);
    TEST_ASSERT_EQUAL_UINT32((1U << 0) | (1U << 8) | (1U << 9), pl011_registers[PL011_CONTROL / sizeof(uint32_t)]);

    UartDevice__enable_receiver_interrupt(&uart);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_INTERRUPTS, pl011_registers[PL011_INTERRUPT_MASK / sizeof(uint32_t)]);
    pl011_registers[PL011_MASKED_INTERRUPT_STATUS / sizeof(uint32_t)] = PL011_RECEIVE_INTERRUPTS;
    pl011_registers[PL011_FLAGS / sizeof(uint32_t)] = 0;
    UartInterruptStatus interrupt_status = UartDevice__interrupt_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_INTERRUPTS, interrupt_status.interrupt);
    TEST_ASSERT_EQUAL_UINT32(0, interrupt_status.device);
    pl011_registers[PL011_DATA / sizeof(uint32_t)] = 'a';
    TEST_ASSERT_TRUE(UartDevice__can_receive(&uart));
    TEST_ASSERT_EQUAL_UINT8('a', UartDevice__read(&uart));
    UartTransmitStatus ready_status = UartDevice__transmit_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(0, ready_status.raw);
    TEST_ASSERT_TRUE(ready_status.ready);
    TEST_ASSERT_TRUE(UartDevice__can_transmit(&uart));
    UartDevice__write(&uart, 'b');
    TEST_ASSERT_EQUAL_UINT32('b', pl011_registers[PL011_DATA / sizeof(uint32_t)]);

    pl011_registers[PL011_FLAGS / sizeof(uint32_t)] = PL011_RECEIVE_EMPTY | PL011_TRANSMIT_FULL;
    TEST_ASSERT_FALSE(UartDevice__can_receive(&uart));
    UartTransmitStatus blocked_status = UartDevice__transmit_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(PL011_RECEIVE_EMPTY | PL011_TRANSMIT_FULL, blocked_status.raw);
    TEST_ASSERT_FALSE(blocked_status.ready);
    TEST_ASSERT_FALSE(UartDevice__can_transmit(&uart));
}

void test_ns16550_uses_byte_registers_for_receive_and_transmit(void)
{
    UartDevice uart = UartDevice__new((Address)ns16550_registers, UART_KIND_NS16550, 0, 1);

    ns16550_registers[NS16550_INTERRUPT_ENABLE] = 0xff;
    UartDevice__configure_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT8(0, ns16550_registers[NS16550_INTERRUPT_ENABLE]);
    UartDevice__enable_receiver_interrupt(&uart);
    TEST_ASSERT_EQUAL_UINT8(NS16550_RECEIVE_INTERRUPT, ns16550_registers[NS16550_INTERRUPT_ENABLE]);

    ns16550_registers[NS16550_INTERRUPT_IDENTIFICATION] = 0x04;
    ns16550_registers[NS16550_LINE_STATUS] = NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY;
    UartInterruptStatus interrupt_status = UartDevice__interrupt_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(0x04, interrupt_status.interrupt);
    TEST_ASSERT_EQUAL_UINT32(NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY, interrupt_status.device);
    ns16550_registers[NS16550_DATA] = 'x';
    TEST_ASSERT_TRUE(UartDevice__can_receive(&uart));
    TEST_ASSERT_EQUAL_UINT8('x', UartDevice__read(&uart));
    UartTransmitStatus ready_status = UartDevice__transmit_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY, ready_status.raw);
    TEST_ASSERT_TRUE(ready_status.ready);
    TEST_ASSERT_TRUE(UartDevice__can_transmit(&uart));
    UartDevice__write(&uart, 'y');
    TEST_ASSERT_EQUAL_UINT8('y', ns16550_registers[NS16550_DATA]);

    UartDevice__acknowledge_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT8(NS16550_RECEIVE_INTERRUPT, ns16550_registers[NS16550_INTERRUPT_ENABLE]);
    ns16550_registers[NS16550_LINE_STATUS] = 0;
    TEST_ASSERT_FALSE(UartDevice__can_receive(&uart));
    UartTransmitStatus blocked_status = UartDevice__transmit_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(0, blocked_status.raw);
    TEST_ASSERT_FALSE(blocked_status.ready);
    TEST_ASSERT_FALSE(UartDevice__can_transmit(&uart));
}

void test_ns16550_uses_configured_word_spaced_registers(void)
{
    UartDevice uart = UartDevice__new((Address)ns16550_word_registers, UART_KIND_NS16550, 2, 4);

    ns16550_word_registers[NS16550_INTERRUPT_ENABLE] = 0xff;
    UartDevice__configure_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT32(0, ns16550_word_registers[NS16550_INTERRUPT_ENABLE]);
    UartDevice__enable_receiver_interrupt(&uart);
    TEST_ASSERT_EQUAL_UINT32(NS16550_RECEIVE_INTERRUPT, ns16550_word_registers[NS16550_INTERRUPT_ENABLE]);

    ns16550_word_registers[NS16550_INTERRUPT_IDENTIFICATION] = 0x0c;
    ns16550_word_registers[NS16550_LINE_STATUS] = NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY;
    UartInterruptStatus interrupt_status = UartDevice__interrupt_status(&uart);
    TEST_ASSERT_EQUAL_UINT32(0x0c, interrupt_status.interrupt);
    TEST_ASSERT_EQUAL_UINT32(NS16550_DATA_READY | NS16550_TRANSMIT_EMPTY, interrupt_status.device);
    ns16550_word_registers[NS16550_DATA] = 'm';
    TEST_ASSERT_TRUE(UartDevice__can_receive(&uart));
    TEST_ASSERT_EQUAL_UINT8('m', UartDevice__read(&uart));
    TEST_ASSERT_TRUE(UartDevice__can_transmit(&uart));
    UartDevice__write(&uart, 'd');
    TEST_ASSERT_EQUAL_UINT32('d', ns16550_word_registers[NS16550_DATA]);

    ns16550_word_registers[NS16550_INTERRUPT_IDENTIFICATION] = 0xc7;
    ns16550_word_registers[31] = 0x06;
    UartDevice__acknowledge_receiver(&uart);
    TEST_ASSERT_EQUAL_UINT32(0x06, ns16550_word_registers[31]);
}

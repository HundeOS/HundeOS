#include <stdint.h>

#define MILKV_DUO_UART_BASE 0x04140000ULL
#define MILKV_DUO_UART_REGISTER_SHIFT 2ULL
#define UART_TRANSMIT 0ULL
#define UART_LINE_STATUS 5ULL
#define UART_TRANSMIT_EMPTY (1U << 5U)
#define FDT_MAGIC 0xd00dfeedU

typedef volatile uint32_t * MilkVDuoUartRegistersPtr;

static MilkVDuoUartRegistersPtr MilkVDuoUart__register(uint64_t index)
{
    return (MilkVDuoUartRegistersPtr)(MILKV_DUO_UART_BASE + (index << MILKV_DUO_UART_REGISTER_SHIFT));
}

static void MilkVDuoUart__put_char(char character)
{
    while ((*MilkVDuoUart__register(UART_LINE_STATUS) & UART_TRANSMIT_EMPTY) == 0)
    {
    }
    *MilkVDuoUart__register(UART_TRANSMIT) = (uint32_t)(uint8_t)character;
}

static void MilkVDuoUart__puts(const char * text)
{
    while (*text != '\0')
    {
        if (*text == '\n')
        {
            MilkVDuoUart__put_char('\r');
        }
        MilkVDuoUart__put_char(*text);
        text++;
    }
}

static void MilkVDuoUart__put_hex(uint64_t value)
{
    static const char digits[] = "0123456789abcdef";
    MilkVDuoUart__puts("0x");
    for (int shift = 60; shift >= 0; shift -= 4)
    {
        MilkVDuoUart__put_char(digits[(value >> shift) & 0xfU]);
    }
}

static uint32_t MilkVDuoBringup__read_big_endian_u32(uint64_t address)
{
    const volatile uint8_t * bytes = (const volatile uint8_t *)address;
    return ((uint32_t)bytes[0] << 24U) | ((uint32_t)bytes[1] << 16U) | ((uint32_t)bytes[2] << 8U) | (uint32_t)bytes[3];
}

void MilkVDuoBringup__main(uint64_t hart_id, uint64_t device_tree)
{
    MilkVDuoUart__puts("HundeOS Milk-V Duo bring-up\n");
    MilkVDuoUart__puts("S-mode entry=0x0000000080200000 hart=");
    MilkVDuoUart__put_hex(hart_id);
    MilkVDuoUart__puts(" dtb=");
    MilkVDuoUart__put_hex(device_tree);
    MilkVDuoUart__puts("\n");

    if (device_tree != 0 && MilkVDuoBringup__read_big_endian_u32(device_tree) == FDT_MAGIC)
    {
        MilkVDuoUart__puts("DTB handoff valid\n");
    }
    else
    {
        MilkVDuoUart__puts("DTB handoff invalid\n");
    }

    MilkVDuoUart__puts("Milk-V Duo platform entry passed\n");
}

#define DRIVER_LOG_TAG "UART-RX"

#include "../../sys/io_server.h"
#include "../../sys/debug.h"
#include "../../sys/service.h"

#define UART0_PHYS_BASE 0x09000000ULL
#define UART_MMIO_SIZE  0x1000ULL
#define UART_IRQ_ID     33ULL

#define UART_DR   0x000
#define UART_FR   0x018
#define UART_ICR  0x044
#define FR_RXFE   (1u << 4)
#define I_RX      (1u << 4)
#define I_RT      (1u << 6)
#define I_ERR     ((1u << 7) | (1u << 8) | (1u << 9) | (1u << 10))

#ifndef UART_RX_ENTRYPOINT
#define UART_RX_ENTRYPOINT main
#endif

#if !defined(UART_READ32) || !defined(UART_WRITE32)
static volatile uint32_t * reg32(void * base, uint64_t offset)
{
    return (volatile uint32_t *) ((uint64_t) base + offset);
}
#endif

#ifndef UART_READ32
#define UART_READ32(base, offset) (*reg32((base), (offset)))
#endif

#ifndef UART_WRITE32
#define UART_WRITE32(base, offset, value) (*reg32((base), (offset)) = (uint32_t) (value))
#endif

static uint64_t drain_uart(void * uart, IOServerBufferDataPtr target, uint64_t capacity)
{
    uint64_t count = 0;
    while (count < capacity && ((UART_READ32(uart, UART_FR) & FR_RXFE) == 0))
    {
        target[count++] = (char) (UART_READ32(uart, UART_DR) & 0xFF);
    }
    UART_WRITE32(uart, UART_ICR, I_RX | I_RT | I_ERR);
    return count;
}

int UART_RX_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("uart_rx: service registration failed\n");
        return 1;
    }

    ServiceMappedMemoryPtr uart_mapping;
    if (!service_ok(service_map_mmio(UART0_PHYS_BASE, UART_MMIO_SIZE, &uart_mapping)))
    {
        DRIVER_DEBUG("uart_rx: mmio map failed\n");
        return 1;
    }
    void * uart = driver_ptr(uart_mapping);

    IOHandle tty = io_server_register(":tty:", IO_SERVER_READ);
    if (tty < 0)
    {
        DRIVER_DEBUG("uart_rx: register error %i\n", (int) tty);
        return 1;
    }

    if (!service_ok(service_subscribe_irq(UART_IRQ_ID)))
    {
        DRIVER_DEBUG("uart_rx: irq subscribe failed\n");
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode wait_status = service_wait(&event);
         service_alive(wait_status);
         wait_status = service_wait(&event))
    {
        if (service_ok(wait_status))
        {
            bool uart_irq = event.type == SERVICE_EVENT_IRQ && event.source == UART_IRQ_ID;
            bool read_request = event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_READ_REQUEST;
            if (uart_irq || read_request)
            {
                IOServerBuffer buffer;
                if (io_server_get_buffer(tty, &buffer) == IO_OK)
                {
                    uint64_t bytes = drain_uart(uart, buffer.data, buffer.capacity);
                    if (bytes > 0)
                    {
                        io_server_data_available(tty, bytes);
                    }
                }
            }
        }
    }

    return 0;
}

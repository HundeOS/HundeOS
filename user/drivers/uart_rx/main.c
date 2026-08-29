#define DRIVER_LOG_TAG "UART-RX"

#include "../../sys/io_server.h"
#include "../../sys/debug.h"
#include "../../sys/service.h"
#include "../../sys/uart_config.h"

#define UART_DR   0x000
#define UART_FR   0x018
#define UART_CR   0x030
#define UART_IMSC 0x038
#define UART_ICR  0x044
#define FR_RXFE   (1u << 4)
#define CR_UARTEN (1u << 0)
#define CR_TXE    (1u << 8)
#define CR_RXE    (1u << 9)
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

static void configure_uart_rx(void * uart)
{
    UART_WRITE32(uart, UART_ICR, I_RX | I_RT | I_ERR);
    UART_WRITE32(uart, UART_IMSC, I_RX | I_RT);
    UART_WRITE32(uart, UART_CR, CR_UARTEN | CR_TXE | CR_RXE);
}

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

    UartDriverConfig config;
    if (!UartDriverConfig__load(&config))
    {
        DRIVER_DEBUG("uart_rx: sysinfo read failed\n");
        return 1;
    }

    ServiceMappedMemoryPtr uart_mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &uart_mapping)))
    {
        DRIVER_DEBUG("uart_rx: mmio map failed\n");
        return 1;
    }
    void * uart = driver_ptr(uart_mapping);
    configure_uart_rx(uart);

    BString tty_path = bstr(":tty:");
    IOHandle tty = io_server_register(&tty_path, IO_SERVER_READ);
    if (tty < 0)
    {
        DRIVER_DEBUG("uart_rx: register error %i\n", (int) tty);
        return 1;
    }

    if (!service_ok(service_subscribe_irq(config.irq)))
    {
        DRIVER_DEBUG("uart_rx: irq subscribe failed\n");
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode wait_status = service_wait(&event);
         service_alive(wait_status);
         wait_status = service_wait(&event))
    {
        DRIVER_DEBUG(
            "uart_rx: wait_status=%i event_type=%i event_source=%i event_data=%i\n",
            (int) wait_status,
            (int) event.type,
            (int) event.source,
            (int) event.data
        );
        if (service_ok(wait_status))
        {
            bool uart_irq = event.type == SERVICE_EVENT_IRQ && event.source == config.irq;
            bool read_request = event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_READ_REQUEST;
            DRIVER_DEBUG(
                "uart_rx: uart_irq=%i read_request=%i\n",
                uart_irq,
                read_request
            );
            if (uart_irq || read_request)
            {
                IOServerBuffer buffer;
                IOStatus buffer_status = io_server_get_buffer(tty, &buffer);
                DRIVER_DEBUG(
                    "uart_rx: get_buffer status=%i capacity=%i data=%p\n",
                    (int) buffer_status,
                    (int) buffer.capacity,
                    buffer.data
                );
                if (buffer_status == IO_OK)
                {
                    uint64_t bytes = drain_uart(uart, buffer.data, buffer.capacity);
                    DRIVER_DEBUG("uart_rx: drained bytes=%i\n", (int) bytes);
                    if (bytes > 0)
                    {
                        DRIVER_DEBUG(
                            "uart_rx: first_byte=%i first_byte_hex=%x\n",
                            (int) ((unsigned char) buffer.data[0]),
                            (unsigned int) ((unsigned char) buffer.data[0])
                        );
                        IOStatus data_status = io_server_data_available(tty, bytes);
                        DRIVER_DEBUG(
                            "uart_rx: data_available status=%i bytes=%i\n",
                            (int) data_status,
                            (int) bytes
                        );
                    }
                }
            }
        }
    }

    return 0;
}

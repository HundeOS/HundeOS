#define DRIVER_LOG_TAG "UART-TX"

#include "sys/io_server.h"
#include "sys/debug.h"
#include "sys/service.h"
#include "sys/uart_config.h"

#define UART_DR   0x000
#define UART_FR   0x018
#define FR_TXFF   (1u << 5)

#ifndef UART_TX_ENTRYPOINT
#define UART_TX_ENTRYPOINT main
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

static void write_uart(void * uart, char value)
{
    while ((UART_READ32(uart, UART_FR) & FR_TXFF) != 0)
    {
    }
    UART_WRITE32(uart, UART_DR, value);
}

int UART_TX_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("uart_tx: service registration failed\n");
        return 1;
    }

    UartDriverConfig config;
    if (!UartDriverConfig__load(&config))
    {
        DRIVER_DEBUG("uart_tx: sysinfo read failed\n");
        return 1;
    }

    ServiceMappedMemoryPtr uart_mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.mmio_size, &uart_mapping)))
    {
        DRIVER_DEBUG("uart_tx: mmio map failed\n");
        return 1;
    }
    void * uart = driver_ptr(uart_mapping);

    IOHandle tty = io_server_register(&bstr(":tty:"), IO_SERVER_WRITE);
    if (tty < 0)
    {
        DRIVER_DEBUG("uart_tx: register error %i\n", (int) tty);
        return 1;
    }

    ServiceEvent event;
    ServiceCode wait_status;
    for (wait_status = service_wait(&event);
         service_alive(wait_status);
         wait_status = service_wait(&event))
    {
        if (service_ok(wait_status))
        {
            bool write_request = event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_WRITE_REQUEST;
            if (write_request)
            {
                IOServerBuffer buffer;
                if (io_server_get_buffer(tty, &buffer) == IO_OK)
                {
                    for (uint64_t i = 0; i < buffer.capacity; i++)
                    {
                        write_uart(uart, buffer.data[i]);
                        if (buffer.data[i] == '\n')
                        {
                            write_uart(uart, '\r');
                        }
                    }
                    io_server_data_consumed(tty, buffer.capacity);
                }
            }
        }
    }

    return 0;
}

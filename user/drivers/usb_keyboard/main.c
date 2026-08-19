#define DRIVER_LOG_TAG "USB-KEYBOARD"

#include <stdbool.h>
#include <stdint.h>

#include "../../sys/debug.h"
#include "../../sys/io_server.h"
#include "../../sys/service.h"

#ifndef USB_KEYBOARD_ENTRYPOINT
#define USB_KEYBOARD_ENTRYPOINT main
#endif

#define USB_KEYBOARD_PATH ":keyboard:"
#define USB_KEYBOARD_PENDING_BYTES 256U

typedef struct UsbKeyboard
{
    IOHandle keyboard;
    char pending[USB_KEYBOARD_PENDING_BYTES];
    uint64_t pending_read;
    uint64_t pending_write;
}
UsbKeyboard;

typedef UsbKeyboard * UsbKeyboardPtr;

static uint64_t UsbKeyboard__pending_count(UsbKeyboardPtr self)
{
    return self->pending_write - self->pending_read;
}

bool UsbKeyboard__pending_push(UsbKeyboardPtr self, char byte)
{
    if (UsbKeyboard__pending_count(self) >= USB_KEYBOARD_PENDING_BYTES)
    {
        return false;
    }

    self->pending[self->pending_write % USB_KEYBOARD_PENDING_BYTES] = byte;
    self->pending_write++;
    return true;
}

static void UsbKeyboard__publish_pending(UsbKeyboardPtr self)
{
    if (UsbKeyboard__pending_count(self) == 0)
    {
        return;
    }

    IOServerBuffer buffer;
    if (io_server_get_buffer(self->keyboard, &buffer) != IO_OK)
    {
        return;
    }

    uint64_t byte_count = 0;
    while (byte_count < buffer.capacity && UsbKeyboard__pending_count(self) > 0)
    {
        buffer.data[byte_count] = self->pending[self->pending_read % USB_KEYBOARD_PENDING_BYTES];
        self->pending_read++;
        byte_count++;
    }

    if (byte_count > 0)
    {
        io_server_data_available(self->keyboard, byte_count);
    }
}

static bool UsbKeyboard__init(UsbKeyboardPtr self)
{
    *self = (UsbKeyboard) {
        .keyboard = -1,
        .pending = { 0 },
        .pending_read = 0,
        .pending_write = 0,
    };

    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("usb_keyboard: service registration failed\n");
        return false;
    }

    self->keyboard = io_server_register(USB_KEYBOARD_PATH, IO_SERVER_READ);
    if (self->keyboard < 0)
    {
        DRIVER_DEBUG("usb_keyboard: register failed %i\n", (int) self->keyboard);
        return false;
    }

    return true;
}

int USB_KEYBOARD_ENTRYPOINT(void)
{
    UsbKeyboard keyboard;
    if (!UsbKeyboard__init(&keyboard))
    {
        return 1;
    }

    ServiceEvent event;
    for (ServiceCode status = service_wait(&event); service_alive(status); status = service_wait(&event))
    {
        if (!service_ok(status))
        {
            continue;
        }

        if (event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_READ_REQUEST)
        {
            UsbKeyboard__publish_pending(&keyboard);
        }
    }

    return 0;
}

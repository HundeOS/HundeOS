#define DRIVER_LOG_TAG "USB-KEYBOARD"

#include <stdbool.h>
#include <stdint.h>

#define RING_BUFFER_DEBUG_PRINTF(level, format, ...)
#include "../../../kernel/types/ring_buffer.h"
#include "../../sys/debug.h"
#include "../../sys/io_server.h"
#include "../../sys/service.h"

#ifndef USB_KEYBOARD_ENTRYPOINT
#define USB_KEYBOARD_ENTRYPOINT main
#endif

#define USB_KEYBOARD_PATH ":keyboard:"
#define USB_KEYBOARD_PENDING_BYTES 256U

RingBufferDefine(USB_KEYBOARD_PENDING_BYTES, UsbKeyboardPendingBuffer)
RingBufferStackBodyDefine(UsbKeyboardPendingBuffer)

typedef struct UsbKeyboard
{
    IOHandle keyboard;
    UsbKeyboardPendingBuffer pending;
}
UsbKeyboard;

typedef UsbKeyboard * UsbKeyboardPtr;

static void UsbKeyboard__publish_pending(UsbKeyboardPtr self)
{
    if (UsbKeyboardPendingBuffer__item_count(&self->pending) == 0)
    {
        return;
    }

    IOServerBuffer buffer;
    if (io_server_get_buffer(self->keyboard, &buffer) != IO_OK)
    {
        return;
    }

    match_result(ItemCount, UsbKeyboardPendingBuffer__drain(&self->pending, (CharPtr) buffer.data, buffer.capacity),
        success(byte_count)
        {
            if (byte_count > 0)
            {
                io_server_data_available(self->keyboard, byte_count);
            }
        },
        error(code, message)
        {
            DRIVER_DEBUG("usb_keyboard: pending drain failed: %s (%i)\n", BString__cstr(&message), (int) code);
            return;
        }
    );
}

static bool UsbKeyboard__init(UsbKeyboardPtr self)
{
    *self = (UsbKeyboard) {
        .keyboard = -1,
        .pending = UsbKeyboardPendingBuffer__new(),
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

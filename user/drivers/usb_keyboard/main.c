#define DRIVER_LOG_TAG "USB-KEYBOARD"

#include <stdbool.h>
#include <stdint.h>

#define RING_BUFFER_DEBUG_PRINTF(level, format, ...)
#include "../../../kernel/types/ring_buffer.h"
#include "sys/debug.h"
#include "io/stream_driver.h"
#include "sys/service.h"

#ifndef USB_KEYBOARD_ENTRYPOINT
#define USB_KEYBOARD_ENTRYPOINT main
#endif

#define USB_KEYBOARD_PENDING_BYTES 256U

RingBufferDefine(USB_KEYBOARD_PENDING_BYTES, UsbKeyboardPendingBuffer)
    RingBufferStackBodyDefine(UsbKeyboardPendingBuffer)

        typedef struct UsbKeyboard
{
    UsbKeyboardPendingBuffer pending;
} UsbKeyboard;

typedef UsbKeyboard * UsbKeyboardPtr;

static UsbKeyboard UsbKeyboard__new(void)
{
    return (UsbKeyboard){
        .pending = UsbKeyboardPendingBuffer__new(),
    };
}

static IOReply UsbKeyboard__read(IOServiceContext context, IORequestPtr request)
{
    UsbKeyboardPtr self = context;
    IOReply reply = IOReply__new(IO_OK, 0);
    if (request->count == 0)
    {
        return reply;
    }
    match_result(
        UsbKeyboardPendingBuffer__drain(&self->pending, (CharPtr)IORequest__read_target(request, &reply),
                                        request->count),
        success(byte_count)
        {
            reply.value = byte_count;
            reply.status = byte_count == 0 ? IO_WAIT_FOR_DATA : IO_OK;
            return reply;
        },
        error(code, message)
        {
            return IOReply__new(IO_PANIC, 0);
        }
    );
}

int USB_KEYBOARD_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        return 1;
    }
    static UsbKeyboard keyboard;
    static IOStreamDriver driver;
    static IOService service;
    keyboard = UsbKeyboard__new();
    driver = IOStreamDriver__new(IO_OPEN_READ, &keyboard, UsbKeyboard__read);
    if (IOService__listen_into(&service, &bstr(IO_KEYBOARD_SERVICE), &driver, IOStreamDriver__execute,
                               IOStreamDriver__disconnect) != IO_OK)
    {
        return 1;
    }
    if (!service_ok(service_publish_ready(&bstr(IO_KEYBOARD_SERVICE))))
    {
        return 1;
    }
    Channel__prepare_wait();
    while (true)
    {
        if (!IOService__poll(&service))
        {
            Channel__wait_event();
        }
    }
}

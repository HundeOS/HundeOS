#include <stdbool.h>
#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

static char fake_io_buffer[64];
static bool fake_became_service;
static bool fake_registered_keyboard;
static bool fake_data_available;
static uint64_t fake_data_available_bytes;
static bool fake_wait_returned_read_request;
static IOHandle fake_register_result;

#define USB_KEYBOARD_ENTRYPOINT usb_keyboard_main_under_test
#include "../../../../user/drivers/usb_keyboard/main.c"

void setUp(void)
{
    for (uint64_t index = 0; index < sizeof(fake_io_buffer); index++)
    {
        fake_io_buffer[index] = '\0';
    }

    fake_became_service = false;
    fake_registered_keyboard = false;
    fake_data_available = false;
    fake_data_available_bytes = 0;
    fake_wait_returned_read_request = false;
    fake_register_result = 5;
}

void tearDown(void)
{
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    if (!fake_wait_returned_read_request)
    {
        fake_wait_returned_read_request = true;
        event->type = SERVICE_EVENT_IO;
        event->source = SERVICE_IO_READ_REQUEST;
        event->data = 0;
        return SERVICE_OK;
    }

    return SERVICE_ABORT;
}

IOHandle io_server_register(ConstBStringPtr path, IOServerRole role)
{
    TEST_ASSERT_EQUAL_STRING(":keyboard:", BString__cstr(path));
    TEST_ASSERT_EQUAL_INT(IO_SERVER_READ, role);
    fake_registered_keyboard = true;
    return fake_register_result;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT64(5, handle);
    *buffer = (IOServerBuffer){
        .data = fake_io_buffer,
        .capacity = sizeof(fake_io_buffer),
    };
    return IO_OK;
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    TEST_ASSERT_EQUAL_INT64(5, handle);
    fake_data_available = true;
    fake_data_available_bytes = byte_count;
    return IO_OK;
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    return IO_OK;
}

IOStatus io_server_eof(IOHandle handle)
{
    return IO_OK;
}

void test_usb_keyboard_registers_keyboard_read_service(void)
{
    TEST_ASSERT_EQUAL_INT(0, usb_keyboard_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_registered_keyboard);
    TEST_ASSERT_FALSE(fake_data_available);
}

void test_usb_keyboard_returns_error_when_keyboard_registration_fails(void)
{
    fake_register_result = -1;

    TEST_ASSERT_EQUAL_INT(1, usb_keyboard_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_registered_keyboard);
}

void test_usb_keyboard_publishes_pending_bytes(void)
{
    UsbKeyboard keyboard = {
        .keyboard = 5,
        .pending = UsbKeyboardPendingBuffer__new(),
    };

    match_result(UsbKeyboardPendingBuffer__push(&keyboard.pending, 'u'),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(UsbKeyboardPendingBuffer__push(&keyboard.pending, 's'),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(UsbKeyboardPendingBuffer__push(&keyboard.pending, 'b'),
        success_do_nothing(),
        error_test_fail()
    );

    UsbKeyboard__publish_pending(&keyboard);

    TEST_ASSERT_TRUE(fake_data_available);
    TEST_ASSERT_EQUAL_UINT64(3, fake_data_available_bytes);
    TEST_ASSERT_EQUAL_MEMORY("usb", fake_io_buffer, 3);
}

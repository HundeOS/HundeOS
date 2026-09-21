#include "unity.h"
#include "../stream_runtime.h"
#define USB_KEYBOARD_ENTRYPOINT usb_keyboard_main_under_test
#include "../../../../user/drivers/usb_keyboard/main.c"

void setUp(void)
{
}
void tearDown(void)
{
}
ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}
void StreamDriverTest__step(IOStreamDriverPtr driver)
{
    TEST_ASSERT_EQUAL(IO_OPEN_READ, driver->access);
    IORequest request = {.count = 1};
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, driver->transfer(driver->context, &request).status);
    StreamDriverTest__stop();
}
void test_usb_keyboard_stub_serves_read_stream(void)
{
    TEST_ASSERT_EQUAL(0, StreamDriverTest__run(usb_keyboard_main_under_test));
}
void test_usb_keyboard_reads_buffer_without_kernel_io(void)
{
    UsbKeyboard keyboard = UsbKeyboard__new();
    match_result(UsbKeyboardPendingBuffer__push(&keyboard.pending, 'u'),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(UsbKeyboardPendingBuffer__push(&keyboard.pending, 's'),
        success_do_nothing(),
        error_test_fail()
    );
    IORequest request = {.count = 1};
    IOReply reply = UsbKeyboard__read(&keyboard, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(1, reply.value);
    TEST_ASSERT_EQUAL('u', reply.data[0]);
    request.count = 0;
    TEST_ASSERT_EQUAL(IO_OK, UsbKeyboard__read(&keyboard, &request).status);
    request.count = 64;
    reply = UsbKeyboard__read(&keyboard, &request);
    TEST_ASSERT_EQUAL(1, reply.value);
    TEST_ASSERT_EQUAL('s', reply.data[0]);
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, UsbKeyboard__read(&keyboard, &request).status);
}

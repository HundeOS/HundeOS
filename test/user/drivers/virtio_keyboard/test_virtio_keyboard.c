#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

static char fake_io_buffer[64];
static bool fake_data_available;
static uint64_t fake_data_available_bytes;

#define VIRTIO_KEYBOARD_ENTRYPOINT virtio_keyboard_main_under_test
#include "../../../../user/drivers/virtio_keyboard/main.c"

void setUp(void)
{
    for (uint64_t index = 0; index < sizeof(fake_io_buffer); index++)
    {
        fake_io_buffer[index] = '\0';
    }
    fake_data_available = false;
    fake_data_available_bytes = 0;
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

void __sync_boundary()
{
}

ServiceCode service_become_service()
{
    return SERVICE_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    return SERVICE_MAP_FAILED;
}

ServiceCode service_device_address(void * driver_address, Address * device_address)
{
    return SERVICE_MAP_FAILED;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    return SERVICE_ABORT;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return 0;
}

IOHandle io_server_register(ConstBStringPtr path, IOServerRole role)
{
    return 5;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT64(5, handle);
    *buffer = (IOServerBuffer) {
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

void test_virtio_keyboard_translates_key_events_to_terminal_bytes(void)
{
    VirtioKeyboard keyboard = {
        .keyboard = 5,
        .shift = false,
        .pending = { 0 },
        .pending_read = 0,
        .pending_write = 0,
    };

    VirtioKeyboard__handle_key_event(&keyboard, 30, 1);
    VirtioKeyboard__handle_key_event(&keyboard, 48, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFTSHIFT, 1);
    VirtioKeyboard__handle_key_event(&keyboard, 46, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFTSHIFT, 0);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_ENTER, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_BACKSPACE, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFT, 1);

    VirtioKeyboard__publish_pending(&keyboard);

    TEST_ASSERT_TRUE(fake_data_available);
    TEST_ASSERT_EQUAL_UINT64(8, fake_data_available_bytes);
    TEST_ASSERT_EQUAL_MEMORY("abC\n\x7f\033[D", fake_io_buffer, 8);
}

void test_virtio_keyboard_ignores_key_release_for_normal_keys(void)
{
    VirtioKeyboard keyboard = {
        .keyboard = 5,
        .shift = false,
        .pending = { 0 },
        .pending_read = 0,
        .pending_write = 0,
    };

    VirtioKeyboard__handle_key_event(&keyboard, 30, 0);
    VirtioKeyboard__publish_pending(&keyboard);

    TEST_ASSERT_FALSE(fake_data_available);
}

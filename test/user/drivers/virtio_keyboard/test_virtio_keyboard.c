#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "unity.h"
#include "ipc/trace_protocol.h"
#include "../stream_runtime.h"

static uint32_t registers[4096];
static unsigned step;
static unsigned acknowledgements;
static bool delivered;
static bool backpressure;
static bool fail_subscribe;

#define TEST_VIRTIO_MMIO_PHYSICAL_BASE 0x0a000000ULL
#define TEST_VIRTIO_MMIO_SLOT_SIZE 0x200ULL
#define TEST_VIRTIO_MMIO_SLOT_COUNT 32ULL
#define TEST_VIRTIO_MMIO_FIRST_IRQ 48ULL

#define VIRTIO_KEYBOARD_ENTRYPOINT virtio_keyboard_main_under_test
#include "../../../../user/drivers/virtio_keyboard/main.c"

void setUp(void)
{
    memset(registers, 0, sizeof(registers));
    registers[VIRTIO_MMIO_MAGIC / 4] = VIRTIO_MMIO_MAGIC_VALUE;
    registers[VIRTIO_MMIO_VERSION / 4] = 1;
    registers[VIRTIO_MMIO_DEVICE_ID / 4] = VIRTIO_DEVICE_ID_INPUT;
    registers[VIRTIO_MMIO_QUEUE_NUM_MAX / 4] = VIRTIO_INPUT_QUEUE_SIZE;
    step = 0;
    acknowledgements = 0;
    delivered = false;
    backpressure = false;
    fail_subscribe = false;
}

void tearDown(void)
{
}

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    return TRACE_OK;
}

void __sync_boundary(void)
{
}

ServiceCode service_become_service(void)
{
    return SERVICE_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_HEX64(TEST_VIRTIO_MMIO_PHYSICAL_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(TEST_VIRTIO_MMIO_SLOT_SIZE * TEST_VIRTIO_MMIO_SLOT_COUNT, size);
    *memory = (ServiceMappedMemoryPtr)registers;
    return SERVICE_OK;
}

Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    BootHardwareInfo info = BootHardwareInfo__new();
    BootVirtioMmioInfo virtio_mmio = BootVirtioMmioInfo__new(TEST_VIRTIO_MMIO_PHYSICAL_BASE, TEST_VIRTIO_MMIO_SLOT_SIZE,
                                                             TEST_VIRTIO_MMIO_SLOT_COUNT, TEST_VIRTIO_MMIO_FIRST_IRQ);
    info.virtio_mmio = Some(BootVirtioMmioInfo, virtio_mmio);
    return Success(BootHardwareInfo, info);
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (char *)registers + offset;
}

ServiceCode service_device_address(void * driver_address, AddressPtr device_address)
{
    *device_address = (Address)driver_address;
    return SERVICE_OK;
}

ServiceCode InterruptClient__subscribe(IRQNumber irq)
{
    TEST_ASSERT_EQUAL(TEST_VIRTIO_MMIO_FIRST_IRQ, irq);
    return fail_subscribe ? SERVICE_ABORT : SERVICE_OK;
}

ServiceCode InterruptClient__poll(ServiceEventPtr event)
{
    if (delivered)
    {
        return SERVICE_EVENT_QUEUE_EMPTY;
    }
    delivered = true;
    *event = (ServiceEvent){.type = SERVICE_EVENT_IRQ, .source = TEST_VIRTIO_MMIO_FIRST_IRQ};
    return SERVICE_OK;
}

ServiceCode InterruptClient__acknowledge(IRQNumber irq)
{
    TEST_ASSERT_EQUAL(TEST_VIRTIO_MMIO_FIRST_IRQ, irq);
    TEST_ASSERT_EQUAL(1, registers[VIRTIO_MMIO_INTERRUPT_ACK / 4]);
    acknowledgements++;
    return SERVICE_OK;
}

void StreamDriverTest__step(IOStreamDriverPtr driver)
{
    VirtioKeyboardPtr keyboard = driver->context;
    TEST_ASSERT_EQUAL(IO_OPEN_READ, driver->access);
    if (step++ == 0)
    {
        if (backpressure)
        {
            for (unsigned i = 0; i < VIRTIO_KEYBOARD_PENDING_BYTES; i++)
            {
                VirtioKeyboard__emit_byte(keyboard, 'x');
            }
        }
        virtio_keyboard_events[0] = (VirtioInputEvent){.type = EV_KEY, .code = KEY_LEFT, .value = 1};
        VirtqUsedPtr used = VirtioKeyboard__used(keyboard);
        used->ring[0] = (VirtqUsedElem){.id = 0, .len = sizeof(VirtioInputEvent)};
        used->idx = 1;
        registers[VIRTIO_MMIO_INTERRUPT_STAT / 4] = 1;
        return;
    }
    IORequest request = {.count = IO_PROTOCOL_DATA_SIZE};
    if (backpressure && step == 2)
    {
        TEST_ASSERT_EQUAL(0, acknowledgements);
        TEST_ASSERT_EQUAL(0, keyboard->last_used_idx);
        IOReply reply = driver->transfer(driver->context, &request);
        TEST_ASSERT_EQUAL(VIRTIO_KEYBOARD_PENDING_BYTES, reply.value);
        return;
    }
    TEST_ASSERT_EQUAL(1, acknowledgements);
    TEST_ASSERT_EQUAL(1, keyboard->last_used_idx);
    IOReply reply = driver->transfer(driver->context, &request);
    TEST_ASSERT_EQUAL(IO_OK, reply.status);
    TEST_ASSERT_EQUAL(3, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("\033[D", reply.data, 3);
    StreamDriverTest__stop();
}

void test_keyboard_stream_services_device_before_interrupt_manager_ack(void)
{
    TEST_ASSERT_EQUAL(0, StreamDriverTest__run(virtio_keyboard_main_under_test));
}

void test_keyboard_retains_irq_and_event_until_buffer_has_space(void)
{
    backpressure = true;
    TEST_ASSERT_EQUAL(0, StreamDriverTest__run(virtio_keyboard_main_under_test));
}

void test_keyboard_fails_when_interrupt_registration_fails(void)
{
    fail_subscribe = true;
    TEST_ASSERT_EQUAL(1, StreamDriverTest__run(virtio_keyboard_main_under_test));
}

void test_virtio_keyboard_translates_key_events_to_terminal_bytes(void)
{
    VirtioKeyboardDevice device = VirtioKeyboardDevice__new(registers, 0, TEST_VIRTIO_MMIO_FIRST_IRQ);
    VirtioKeyboard keyboard = VirtioKeyboard__new(&device);
    VirtioKeyboard__handle_key_event(&keyboard, 30, 1);
    VirtioKeyboard__handle_key_event(&keyboard, 48, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFTSHIFT, 1);
    VirtioKeyboard__handle_key_event(&keyboard, 46, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFTSHIFT, 0);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_ENTER, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_BACKSPACE, 1);
    VirtioKeyboard__handle_key_event(&keyboard, KEY_LEFT, 1);
    IORequest request = {.count = 3};
    IOReply reply = VirtioKeyboard__read(&keyboard, &request);
    TEST_ASSERT_EQUAL(3, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("abC", reply.data, 3);
    request.count = 64;
    reply = VirtioKeyboard__read(&keyboard, &request);
    TEST_ASSERT_EQUAL(5, reply.value);
    TEST_ASSERT_EQUAL_MEMORY("\n\x7f\033[D", reply.data, 5);
}

void test_virtio_keyboard_empty_and_zero_length_reads(void)
{
    VirtioKeyboardDevice device = VirtioKeyboardDevice__new(registers, 0, TEST_VIRTIO_MMIO_FIRST_IRQ);
    VirtioKeyboard keyboard = VirtioKeyboard__new(&device);
    VirtioKeyboard__handle_key_event(&keyboard, 30, 0);
    IORequest request = {.count = 1};
    TEST_ASSERT_EQUAL(IO_WAIT_FOR_DATA, VirtioKeyboard__read(&keyboard, &request).status);
    request.count = 0;
    TEST_ASSERT_EQUAL(IO_OK, VirtioKeyboard__read(&keyboard, &request).status);
}

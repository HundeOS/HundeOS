#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <setjmp.h>

#include "unity.h"
#include "ipc/trace_protocol.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

#define TEST_PCI_ECAM_SIZE 0x200000ULL

static uint8_t fake_ecam[TEST_PCI_ECAM_SIZE];
static ServiceMappedMemory fake_ecam_mapping;
static bool fake_became_service;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static bool fake_mapped_ecam;
static jmp_buf test_finished;
static bool fake_listening;
static bool fake_ready;
static const char * fake_sysinfo = "pci.present=1\n"
                                   "pci.ecam.physical_base=0x4010000000\n"
                                   "pci.ecam.size=0x200000\n"
                                   "pci.bus.start=0\n"
                                   "pci.bus.end=1\n";

#define HUNDEBUS_ENTRYPOINT hundebus_main_under_test
#define IO__printf test_printf
#include "../../../../user/drivers/hunde-bus/main.c"
#undef IO__printf

static uint64_t TestHundebus__function_offset(uint64_t bus, uint64_t device, uint64_t function)
{
    PciConfig config = {
        .ecam_physical_base = 0x4010000000,
        .ecam_size = TEST_PCI_ECAM_SIZE,
        .bus_start = 0,
        .bus_end = 1,
    };
    return Hundebus__config_offset(&config, bus, device, function);
}

static void TestHundebus__write8(uint64_t offset, uint8_t value)
{
    TEST_ASSERT_LESS_THAN_UINT64(sizeof(fake_ecam), offset);
    fake_ecam[offset] = value;
}

static void TestHundebus__write16(uint64_t offset, uint16_t value)
{
    TEST_ASSERT_LESS_THAN_UINT64(sizeof(fake_ecam) - 1, offset);
    fake_ecam[offset] = (uint8_t)(value & 0xffU);
    fake_ecam[offset + 1] = (uint8_t)(value >> 8);
}

static void TestHundebus__seed_device(uint64_t bus, uint64_t device, uint64_t function, uint16_t vendor_id,
                                      uint16_t device_id, uint8_t class_code, uint8_t subclass, uint8_t prog_if,
                                      uint8_t header_type)
{
    uint64_t offset = TestHundebus__function_offset(bus, device, function);
    TestHundebus__write16(offset + PCI_CONFIG_VENDOR_ID, vendor_id);
    TestHundebus__write16(offset + PCI_CONFIG_DEVICE_ID, device_id);
    TestHundebus__write8(offset + PCI_CONFIG_CLASS_CODE, class_code);
    TestHundebus__write8(offset + PCI_CONFIG_SUBCLASS, subclass);
    TestHundebus__write8(offset + PCI_CONFIG_PROG_IF, prog_if);
    TestHundebus__write8(offset + PCI_CONFIG_HEADER_TYPE, header_type);
}

void setUp(void)
{
    for (uint64_t index = 0; index < sizeof(fake_ecam); index++)
    {
        fake_ecam[index] = 0xff;
    }

    fake_ecam_mapping = (ServiceMappedMemory){
        .driver_address = fake_ecam,
        .device_address = 0x4010000000,
        .size = TEST_PCI_ECAM_SIZE,
    };
    fake_became_service = false;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    fake_mapped_ecam = false;
    fake_listening = false;
    fake_ready = false;
}

void tearDown(void)
{
}

TraceStatus DriverTrace__writef(const char * tag, const char * format, ...)
{
    return TRACE_OK;
}

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(0x4010000000, physical_address);
    TEST_ASSERT_EQUAL_UINT64(TEST_PCI_ECAM_SIZE, size);
    *memory = &fake_ecam_mapping;
    fake_mapped_ecam = true;
    return SERVICE_OK;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    TEST_ASSERT_LESS_THAN_UINT64(memory->size, offset);
    return (void *)((uint64_t)memory->driver_address + offset);
}

size_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), *path);
    TEST_ASSERT_EQUAL_INT(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return Success(IOHandle, 1);
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buf, size_t size)
{
    TEST_ASSERT_EQUAL_INT(1, handle);
    uint64_t index = 0;
    while (fake_sysinfo[index] != '\0' && index < size)
    {
        buf[index] = fake_sysinfo[index];
        index++;
    }
    return Success(ByteCount, (ByteCount)index);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT(1, handle);
    fake_sysinfo_closed = true;
    return Success(IOStatus, IO_OK);
}

Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    TEST_ASSERT_EQUAL_INT(1, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    DynString sysinfo;
    match_result(
        DynString__init(&sysinfo, 128),
        success_do_nothing(),
        error(code, message)
        {
            return Error(DynString, message, code);
        }
    );
    for (ByteCount index = 0; fake_sysinfo[index] != '\0' && index < max_bytes; index++)
    {
        match_result(
            DynString__push_char(&sysinfo, fake_sysinfo[index]),
            success_do_nothing(),
            error(code, message)
            {
                DynString__dispose(&sysinfo);
                return Error(DynString, message, code);
            }
        );
    }
    return Success(DynString, sysinfo);
}

IOStatus IOService__listen_into(IOServicePtr self, ConstBStringPtr name, IOServiceContext context,
                                IOServiceExecute execute, IOServiceDisconnect disconnect)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr(BUS_INFO_SERVICE), *name);
    TEST_ASSERT_EQUAL_PTR(BusInfo__execute, execute);
    TEST_ASSERT_EQUAL_PTR(BusInfo__disconnect, disconnect);
    fake_listening = true;
    *self = (IOService){.context = context, .execute = execute};
    return IO_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    TEST_ASSERT_TRUE(fake_listening);
    TEST_ASSERT_EQUAL_BSTRING(bstr(BUS_INFO_SERVICE), *name);
    fake_ready = true;
    return SERVICE_OK;
}

bool IOService__poll(IOServicePtr self)
{
    TEST_ASSERT_TRUE(fake_ready);
    IORequest request = BusInfo__request(0);
    match_optional(
        self->execute(self->context, 1, 42, &request),
        some(reply)
        {
            TEST_ASSERT_EQUAL(IO_OK, reply.status);
            BusInfoPage page;
            memcpy(&page, reply.data, sizeof(page));
            TEST_ASSERT_EQUAL(1, page.total);
            TEST_ASSERT_EQUAL(0x1b36, page.device.vendor_id);
        },
        none()
        {
            TEST_FAIL_MESSAGE("Missing bus reply");
        }
    );
    longjmp(test_finished, 1);
}

void Channel__prepare_wait(void)
{
}

void Channel__wait_event(void)
{
}

Result(ChannelReplySize) Channel__submit(ChannelPtr self, ConstChannelDataPtr request, size_t request_size,
                                         ChannelDataPtr reply, size_t reply_capacity)
{
    return Error(ChannelReplySize, bstr("Unused in hardware tests"), CHANNEL_UNAVAILABLE);
}

void test_hundebus_config_loads_ecam_from_sysinfo(void)
{
    PciConfig config = {0};

    TEST_ASSERT_TRUE(PciConfig__load(&config));

    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_EQUAL_UINT64(0x4010000000, config.ecam_physical_base);
    TEST_ASSERT_EQUAL_UINT64(TEST_PCI_ECAM_SIZE, config.ecam_size);
    TEST_ASSERT_EQUAL_UINT64(0, config.bus_start);
    TEST_ASSERT_EQUAL_UINT64(1, config.bus_end);
}

void test_hundebus_scan_enumerates_single_and_multifunction_devices(void)
{
    Hundebus hundebus = {
        .pci_config =
            {
                .ecam_physical_base = 0x4010000000,
                .ecam_size = TEST_PCI_ECAM_SIZE,
                .bus_start = 0,
                .bus_end = 1,
            },
        .ecam = Some(ServiceMappedMemoryPtr, &fake_ecam_mapping),
        .pci_devices = BusPciDevices__new(),
    };
    TestHundebus__seed_device(0, 1, 0, 0x1234, 0x5678, 0x0c, 0x03, 0x30, PCI_HEADER_TYPE_MULTIFUNCTION);
    TestHundebus__seed_device(0, 1, 1, 0x1234, 0x1111, 0x0c, 0x03, 0x00, 0x00);
    TestHundebus__seed_device(1, 2, 0, 0x1af4, 0x1000, 0x02, 0x00, 0x00, 0x00);

    uint64_t found = Hundebus__scan_pci(&hundebus);

    TEST_ASSERT_EQUAL_UINT64(3, found);
    TEST_ASSERT_EQUAL_UINT64(3, BusPciDevices__len(&hundebus.pci_devices));
}

void test_hundebus_scan_stops_at_mapped_ecam_size(void)
{
    Hundebus hundebus = {
        .pci_config =
            {
                .ecam_physical_base = 0x4010000000,
                .ecam_size = 0xffff,
                .bus_start = 0,
                .bus_end = 1,
            },
        .ecam = Some(ServiceMappedMemoryPtr, &fake_ecam_mapping),
        .pci_devices = BusPciDevices__new(),
    };
    fake_ecam_mapping.size = 0xffff;
    TestHundebus__seed_device(0, 1, 0, 0x1234, 0x5678, 0x0c, 0x03, 0x30, 0x00);

    uint64_t found = Hundebus__scan_pci(&hundebus);

    TEST_ASSERT_EQUAL_UINT64(1, found);
    TEST_ASSERT_EQUAL_UINT64(1, BusPciDevices__len(&hundebus.pci_devices));
}

void test_hundebus_main_listens_and_serves_channel_query(void)
{
    TestHundebus__seed_device(0, 0, 0, 0x1b36, 0x0008, 0x06, 0x00, 0x00, 0x00);
    if (setjmp(test_finished) == 0)
    {
        hundebus_main_under_test();
        TEST_FAIL_MESSAGE("Bus service terminated");
    }
    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_mapped_ecam);
    TEST_ASSERT_TRUE(fake_listening);
    TEST_ASSERT_TRUE(fake_ready);
}

void test_hundebus_does_not_publish_truncated_inventory(void)
{
    for (uint64_t bus = 0; bus < 2; bus++)
    {
        for (uint64_t device = 0; device < PCI_DEVICE_COUNT; device++)
        {
            for (uint64_t function = 0; function < PCI_FUNCTION_COUNT; function++)
            {
                TestHundebus__seed_device(bus, device, function, 1, 2, 0, 0, 0, PCI_HEADER_TYPE_MULTIFUNCTION);
            }
        }
    }
    TEST_ASSERT_EQUAL(1, hundebus_main_under_test());
    TEST_ASSERT_FALSE(fake_listening);
    TEST_ASSERT_FALSE(fake_ready);
}

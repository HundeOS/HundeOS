#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    uint64_t size;
};

#define TEST_PCI_ECAM_SIZE 0x200000ULL

static uint8_t fake_ecam[TEST_PCI_ECAM_SIZE];
static ServiceMappedMemory fake_ecam_mapping;
static bool fake_became_service;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static bool fake_mapped_ecam;
static bool fake_businfo_registered;
static bool fake_businfo_buffer_requested;
static bool fake_businfo_available;
static bool fake_businfo_eof;
static char fake_businfo_buffer[4096];
static uint64_t fake_businfo_byte_count;
static ServiceEvent fake_wait_events[4];
static uint64_t fake_wait_event_count;
static uint64_t fake_wait_event_index;
static char printed_output[1024];
static uint64_t printed_output_len;
static const char * fake_sysinfo =
    "pci.present=1\n"
    "pci.ecam.physical_base=0x4010000000\n"
    "pci.ecam.size=0x200000\n"
    "pci.bus.start=0\n"
    "pci.bus.end=1\n";

#define HUNDEBUS_ENTRYPOINT hundebus_main_under_test
#define printf test_printf
#include "../../../../user/drivers/hundebus/main.c"
#undef printf

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
    fake_ecam[offset] = (uint8_t) (value & 0xffU);
    fake_ecam[offset + 1] = (uint8_t) (value >> 8);
}

static void TestHundebus__seed_device(
    uint64_t bus,
    uint64_t device,
    uint64_t function,
    uint16_t vendor_id,
    uint16_t device_id,
    uint8_t class_code,
    uint8_t subclass,
    uint8_t prog_if,
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

    fake_ecam_mapping = (ServiceMappedMemory) {
        .driver_address = fake_ecam,
        .device_address = 0x4010000000,
        .size = TEST_PCI_ECAM_SIZE,
    };
    fake_became_service = false;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    fake_mapped_ecam = false;
    fake_businfo_registered = false;
    fake_businfo_buffer_requested = false;
    fake_businfo_available = false;
    fake_businfo_eof = false;
    fake_businfo_byte_count = 0;
    fake_wait_event_count = 0;
    fake_wait_event_index = 0;
    printed_output_len = 0;
    printed_output[0] = '\0';
    for (uint64_t index = 0; index < sizeof(fake_businfo_buffer); index++)
    {
        fake_businfo_buffer[index] = '\0';
    }
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

ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(0x4010000000, physical_address);
    TEST_ASSERT_EQUAL_UINT64(TEST_PCI_ECAM_SIZE, size);
    *memory = &fake_ecam_mapping;
    fake_mapped_ecam = true;
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    if (fake_wait_event_index >= fake_wait_event_count)
    {
        return SERVICE_ABORT;
    }

    *event = fake_wait_events[fake_wait_event_index++];
    return SERVICE_OK;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    TEST_ASSERT_LESS_THAN_UINT64(memory->size, offset);
    return (void *) ((uint64_t) memory->driver_address + offset);
}

IOHandle open(const char * path, Flags flags)
{
    TEST_ASSERT_EQUAL_STRING("/proc/sysinfo", path);
    TEST_ASSERT_EQUAL_INT(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return 1;
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    TEST_ASSERT_EQUAL_INT(1, handle);
    uint64_t index = 0;
    while (fake_sysinfo[index] != '\0' && index < size)
    {
        buf[index] = fake_sysinfo[index];
        index++;
    }
    return (ByteCount) index;
}

IOStatus close(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT(1, handle);
    fake_sysinfo_closed = true;
    return IO_OK;
}

IOHandle io_server_register(const char * path, IOServerRole role)
{
    TEST_ASSERT_EQUAL_STRING("/dev/businfo", path);
    TEST_ASSERT_EQUAL_INT(IO_SERVER_READ, role);
    fake_businfo_registered = true;
    return 7;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT(7, handle);
    fake_businfo_buffer_requested = true;
    *buffer = (IOServerBuffer) {
        .data = fake_businfo_buffer,
        .capacity = sizeof(fake_businfo_buffer),
    };
    return IO_OK;
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    TEST_ASSERT_EQUAL_INT(7, handle);
    fake_businfo_available = true;
    fake_businfo_byte_count = byte_count;
    return IO_OK;
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    TEST_FAIL();
    return IO_OPERATION_NOT_SUPPORTED;
}

IOStatus io_server_eof(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT(7, handle);
    fake_businfo_eof = true;
    return IO_OK;
}

int test_printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    int count = vsnprintf(
        &printed_output[printed_output_len],
        sizeof(printed_output) - printed_output_len,
        format,
        args
    );
    va_end(args);
    if (count > 0)
    {
        printed_output_len += (uint64_t) count;
        if (printed_output_len >= sizeof(printed_output))
        {
            printed_output_len = sizeof(printed_output) - 1;
        }
    }
    return count;
}

void test_hundebus_config_loads_ecam_from_sysinfo(void)
{
    PciConfig config = { 0 };

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
        .pci_config = {
            .ecam_physical_base = 0x4010000000,
            .ecam_size = TEST_PCI_ECAM_SIZE,
            .bus_start = 0,
            .bus_end = 1,
        },
        .ecam = &fake_ecam_mapping,
        .pci_devices = HundebusPciDevices__new(),
    };
    TestHundebus__seed_device(0, 1, 0, 0x1234, 0x5678, 0x0c, 0x03, 0x30, PCI_HEADER_TYPE_MULTIFUNCTION);
    TestHundebus__seed_device(0, 1, 1, 0x1234, 0x1111, 0x0c, 0x03, 0x00, 0x00);
    TestHundebus__seed_device(1, 2, 0, 0x1af4, 0x1000, 0x02, 0x00, 0x00, 0x00);

    uint64_t found = Hundebus__scan_pci(&hundebus);

    TEST_ASSERT_EQUAL_UINT64(3, found);
    TEST_ASSERT_EQUAL_UINT64(3, HundebusPciDevices__len(&hundebus.pci_devices));
}

void test_hundebus_builds_businfo_from_scan(void)
{
    Hundebus hundebus = {
        .pci_config = {
            .ecam_physical_base = 0x4010000000,
            .ecam_size = TEST_PCI_ECAM_SIZE,
            .bus_start = 0,
            .bus_end = 1,
        },
        .ecam = &fake_ecam_mapping,
        .pci_devices = HundebusPciDevices__new(),
    };
    ResultMatch(DynStringCode, DynString__init(&hundebus.businfo, HUNDEBUS_BUSINFO_INITIAL_CAPACITY))
        IsError(code, message, TEST_FAIL())
    End
    TestHundebus__seed_device(0, 1, 0, 0x1234, 0x5678, 0x0c, 0x03, 0x30, 0x00);

    TEST_ASSERT_EQUAL_UINT64(1, Hundebus__scan_pci(&hundebus));
    TEST_ASSERT_TRUE(Hundebus__build_businfo(&hundebus));

    TEST_ASSERT_NOT_NULL(strstr(DynString__cstr(&hundebus.businfo), "pci.present=1\n"));
    TEST_ASSERT_NOT_NULL(strstr(DynString__cstr(&hundebus.businfo), "pci.device.count=1\n"));
    TEST_ASSERT_NOT_NULL(strstr(DynString__cstr(&hundebus.businfo), "pci.device.0.bdf=0:1.0\n"));
    TEST_ASSERT_NOT_NULL(strstr(DynString__cstr(&hundebus.businfo), "pci.device.0.vendor_id=0x1234\n"));
    TEST_ASSERT_NOT_NULL(strstr(DynString__cstr(&hundebus.businfo), "pci.device.0.device_id=0x5678\n"));

    DynString__dispose(&hundebus.businfo);
}

void test_hundebus_main_registers_businfo_and_serves_read_request(void)
{
    TestHundebus__seed_device(0, 0, 0, 0x1b36, 0x0008, 0x06, 0x00, 0x00, 0x00);
    fake_wait_event_count = 1;
    fake_wait_events[0] = (ServiceEvent) {
        .type = SERVICE_EVENT_IO,
        .source = SERVICE_IO_READ_REQUEST,
        .data = 7,
    };

    TEST_ASSERT_EQUAL_INT(0, hundebus_main_under_test());
    TEST_ASSERT_EQUAL_STRING("", printed_output);

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_mapped_ecam);
    TEST_ASSERT_TRUE(fake_businfo_registered);
    TEST_ASSERT_TRUE(fake_businfo_buffer_requested);
    TEST_ASSERT_TRUE(fake_businfo_available);
    TEST_ASSERT_TRUE(fake_businfo_eof);
    TEST_ASSERT_GREATER_THAN_UINT64(0, fake_businfo_byte_count);
    TEST_ASSERT_NOT_NULL(strstr(fake_businfo_buffer, "pci.device.count=1\n"));
    TEST_ASSERT_NOT_NULL(strstr(fake_businfo_buffer, "pci.device.0.vendor_id=0x1b36\n"));
}

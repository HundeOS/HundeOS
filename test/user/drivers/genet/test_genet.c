#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>

#include "unity.h"

#include "../../../../common/types/dyn_string.h"
#include "../../../../common/types/result.h"
#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/syslog.h"
#include "ipc/trace_protocol.h"

#define FAKE_GENET_PHYSICAL_BASE 0xfd580000ULL
#define FAKE_GENET_MMIO_SIZE 0x10000ULL
#define FAKE_GENET_IRQ 157ULL

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    size_t size;
};

static uint32_t fake_sys_rev_ctrl;
static ServiceMappedMemory fake_mapping;
static bool fake_became_service;
static bool fake_mapped_mmio;
static bool fake_ready_published;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static BString fake_ready_path;
static const char * fake_sysinfo;

static ServiceMappedMemory ServiceMappedMemory__new(void * driver_address, uint64_t device_address, size_t size)
{
    return (ServiceMappedMemory){
        .driver_address = driver_address,
        .device_address = device_address,
        .size = size,
    };
}

void setUp(void)
{
    fake_sys_rev_ctrl = 5U << 24U;
    fake_mapping = ServiceMappedMemory__new(&fake_sys_rev_ctrl, FAKE_GENET_PHYSICAL_BASE, FAKE_GENET_MMIO_SIZE);
    fake_became_service = false;
    fake_mapped_mmio = false;
    fake_ready_published = false;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    fake_ready_path = bstr("");
    fake_sysinfo = "ethernet.present=1\n"
                   "ethernet.mac_kind=1\n"
                   "ethernet.physical_base=0xfd580000\n"
                   "ethernet.mmio_size=0x10000\n"
                   "ethernet.irq=157\n";
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
    TEST_ASSERT_EQUAL_UINT64(FAKE_GENET_PHYSICAL_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(FAKE_GENET_MMIO_SIZE, size);
    *memory = &fake_mapping;
    fake_mapped_mmio = true;
    return SERVICE_OK;
}

ServiceCode service_publish_ready(ConstBStringPtr name)
{
    fake_ready_published = true;
    fake_ready_path = *name;
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    return SERVICE_ABORT;
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->driver_address;
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/boot/sysinfo"), *path);
    TEST_ASSERT_EQUAL_INT(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return Success(IOHandle, 3);
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buf,
                                 size_t size) // hundelint: ignore-no-raw-char-buffer
{
    TEST_ASSERT_EQUAL_INT(3, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    size_t index = 0;
    while (fake_sysinfo[index] != '\0' && index < size)
    {
        buf[index] = fake_sysinfo[index];
        index++;
    }
    return Success(ByteCount, (ByteCount)index);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT(3, handle);
    fake_sysinfo_closed = true;
    return Success(IOStatus, IO_OK);
}

Result(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    TEST_ASSERT_EQUAL_INT(3, handle);
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

#define main genet_main_under_test
#include "../../../../user/drivers/genet/main.c"
#undef main

void test_ethernet_config_parses_genet_sysinfo(void)
{
    DynString sysinfo;
    match_result(DynString__init(&sysinfo, 128),
        success_do_nothing(),
        error_test_fail()
    );
    for (uint64_t index = 0; fake_sysinfo[index] != '\0'; index++)
    {
        match_result(DynString__push_char(&sysinfo, fake_sysinfo[index]),
            success_do_nothing(),
            error_test_fail()
        );
    }

    match_optional(
        EthernetConfig__parse(&sysinfo),
        some(config)
        {
            TEST_ASSERT_EQUAL_INT(ETHERNET_MAC_KIND_BCM2711_GENET, config.mac_kind);
            TEST_ASSERT_EQUAL_UINT64(FAKE_GENET_PHYSICAL_BASE, config.physical_base);
            TEST_ASSERT_EQUAL_UINT64(FAKE_GENET_MMIO_SIZE, config.mmio_size);
            TEST_ASSERT_EQUAL_UINT64(FAKE_GENET_IRQ, config.irq);
        },
        none_test_fail()
    );

    DynString__dispose(&sysinfo);
}

void test_genet_main_maps_supported_pi4_mac_and_publishes_ready(void)
{
    int result = genet_main_under_test();

    TEST_ASSERT_EQUAL_INT(0, result);
    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_mapped_mmio);
    TEST_ASSERT_TRUE(fake_ready_published);
    TEST_ASSERT_EQUAL_BSTRING(bstr(GENET_READY_PATH), fake_ready_path);
}

void test_genet_main_rejects_non_genet_config(void)
{
    fake_sysinfo = "ethernet.present=1\n"
                   "ethernet.mac_kind=2\n"
                   "ethernet.physical_base=0xfd580000\n"
                   "ethernet.mmio_size=0x10000\n"
                   "ethernet.irq=157\n";

    int result = genet_main_under_test();

    TEST_ASSERT_EQUAL_INT(1, result);
    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_FALSE(fake_mapped_mmio);
    TEST_ASSERT_FALSE(fake_ready_published);
}

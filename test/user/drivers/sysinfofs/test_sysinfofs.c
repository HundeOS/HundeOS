#include "unity.h"
#include <string.h>
#include "../../../../user/drivers/sysinfofs/sysinfofs.h"
#include "sys/framebuffer_config.h"
#include "sys/uart_config.h"

static BootHardwareInfo hardware;
void setUp(void)
{
    hardware = BootHardwareInfo__new();
}
void tearDown(void)
{
}
Result(BootHardwareInfo) BootHardwareInfo__load(void)
{
    return Success(BootHardwareInfo, hardware);
}
FilesystemOperations FilesystemOperations__new(FilesystemLoad load, FilesystemStore store, FilesystemList list,
                                               FilesystemMkdir mkdir, FilesystemRemove remove,
                                               FilesystemFileSize capacity)
{
    return (FilesystemOperations){load, store, list, mkdir, remove, capacity};
}

void test_sysinfo_renders_configuration_readable_by_existing_drivers(void)
{
    hardware.uart = BootUartInfo__new(0x9000000, 4096, 33, UART_KIND_PL011);
    hardware.framebuffer =
        Some(BootFramebufferInfo, BootFramebufferInfo__new(0x30000000, 8192, 64, 32, 64, 0, 0, 0, 0, 0));
    uint8_t data[FILESYSTEM_FILE_CAPACITY + 1] = {0};
    match_result(
        Sysinfofs__render(&hardware, data),
        success(size)
        {
            TEST_ASSERT_GREATER_THAN(0, size);
            data[size] = 0;
        },
        error_test_fail()
    );
    DynString text;
    match_result(DynString__init(&text, FILESYSTEM_FILE_CAPACITY),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(DynString__append_cstr(&text, (const char *)data),
        success_do_nothing(),
        error_test_fail()
    );
    FramebufferConfig framebuffer = FramebufferConfig__new();
    TEST_ASSERT_TRUE(FramebufferConfig__read_config(&text, &framebuffer));
    TEST_ASSERT_EQUAL_UINT64(64, framebuffer.width);
    UartDriverConfig uart = {0};
    TEST_ASSERT_TRUE(UartDriverConfig__read_config(&text, &uart));
    TEST_ASSERT_EQUAL_UINT64(33, uart.irq);
    TEST_ASSERT_EQUAL(UART_KIND_PL011, uart.kind);
    DynString__dispose(&text);
}

void test_sysinfo_operations_are_read_only_and_reject_unknown_paths(void)
{
    FilesystemOperations operations = Sysinfofs__operations();
    uint8_t data[FILESYSTEM_FILE_CAPACITY];
    match_result(operations.load(&hardware, &bstr("/missing"), data),
        success_test_fail(),
        error_test_ok()
    );
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, operations.store(&hardware, &bstr("/sysinfo"), data, 0));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, operations.mkdir(&hardware, &bstr("/directory")));
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, operations.remove(&hardware, &bstr("/sysinfo"), 0));
    match_result(
        operations.list(&hardware, &bstr("/"), 0),
        success(entry)
        {
            match_optional(entry,
                some(value)
                {
                    TEST_ASSERT_EQUAL_STRING("sysinfo", value.name);
                },
                none_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_sysinfo_renders_plic_specific_controller_fields(void)
{
    BootPlicInfo plic = BootPlicInfo__new(0xc000000, 95);
    hardware.interrupt_controller = BootInterruptControllerInfo__new_plic(&plic);
    uint8_t data[FILESYSTEM_FILE_CAPACITY + 1] = {0};
    match_result(Sysinfofs__render(&hardware, data),
        success(size)
        {
            data[size] = 0;
        },
        error_test_fail()
    );

    TEST_ASSERT_NOT_NULL(strstr((const char *)data, "interrupt_controller.kind=3\n"));
    TEST_ASSERT_NOT_NULL(strstr((const char *)data, "interrupt_controller.plic.physical_base=201326592\n"));
    TEST_ASSERT_NOT_NULL(strstr((const char *)data, "interrupt_controller.plic.source_count=95\n"));
    TEST_ASSERT_NULL(strstr((const char *)data, "interrupt_controller.distributor_physical_base"));
}

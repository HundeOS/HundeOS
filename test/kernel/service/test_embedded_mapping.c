#include "unity.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../../../common/ipc/buffer.h"
#include "../../../kernel/bin/embedded_mapping.h"
#include <string.h>

static KernelPtr kernel;
static ProcessPtr process;
static _Alignas(4096) uint8_t archive[4096];

void setUp(void)
{
    kernel = init_test_kernel();
    process = dummy_process(20, 0, PROC_STATE_READY);
    process->name = bstr(EMBEDDED_FS_DRIVER);
    memset(archive, 0, sizeof(archive));
    EmbeddedFsHeaderPtr header = (EmbeddedFsHeaderPtr)archive;
    *header = (EmbeddedFsHeader){.magic = EMBEDDED_FS_MAGIC,
                                 .version = EMBEDDED_FS_VERSION,
                                 .index_offset = sizeof(EmbeddedFsHeader),
                                 .names_offset = sizeof(EmbeddedFsHeader),
                                 .data_offset = sizeof(EmbeddedFsHeader),
                                 .blob_size = sizeof(EmbeddedFsHeader)};
    SystemInfo info = SystemInfo__new();
    info.embedded_binaries.virtual_start = (Address)archive;
    info.embedded_binaries.physical_start = 0x90000000;
    info.embedded_binaries.physical_end = 0x90001000;
    kernel->system_info = Some(SystemInfo, info);
}

void tearDown(void)
{
}

void test_archive_mapping_does_not_overlap_shared_buffers(void)
{
    Address buffer_end = BUFFER_BASE + BUFFER_LIMIT * BUFFER_SIZE;
    TEST_ASSERT_TRUE(buffer_end <= EMBEDDED_FS_VADDR);
}

void test_archive_is_only_mapped_into_bootstrap_consumers_read_only_and_non_executable(void)
{
    process->name = bstr("/sbin/ls");
    match_result(EmbeddedArchive__map_process(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), EMBEDDED_FS_VADDR, true,
                                           false, false, sizeof(archive)));
    process->name = bstr(EMBEDDED_FS_DRIVER);
    match_result(EmbeddedArchive__map_process(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), EMBEDDED_FS_VADDR, true,
                                          false, false, sizeof(archive)));
}

void test_io_host_receives_the_same_read_only_non_executable_archive(void)
{
    process->name = bstr(EMBEDDED_FS_HOST);
    match_result(EmbeddedArchive__map_process(process),
        success_do_nothing(),
        error_test_fail()
    );
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), EMBEDDED_FS_VADDR, true,
                                          false, false, sizeof(archive)));
}

void test_invalid_archive_does_not_get_mapped(void)
{
    archive[0] = 0;
    match_result(
        EmbeddedArchive__map_process(process),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_FAILED_TO_SETUP, code);
        }
    );
    TEST_ASSERT_FALSE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), EMBEDDED_FS_VADDR, true,
                                           false, false, sizeof(archive)));
}

void test_partial_last_page_is_not_exposed_to_userspace(void)
{
    match_optional(
        kernel->system_info,
        some(info)
        {
            info.embedded_binaries.physical_end--;
            kernel->system_info = Some(SystemInfo, info);
        },
        none_test_fail()
    );
    match_result(
        EmbeddedArchive__map_process(process),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(PROCESS_FAILED_TO_SETUP, code);
        }
    );
}

#include <string.h>

#include "unity.h"

#include "../../../kernel/syscall/syslog_handler.h"
#include "../../../common_includes/syscalls/syslog.h"
#include "../fixtures/process.h"
#include "../stubs/panic.h"

void BasicIO__reset_output();
const char * BasicIO__output();

void setUp(void)
{
    clear_panic_state();
    BasicIO__reset_output();
}

void tearDown(void)
{
    TEST_ASSERT_FALSE(panic_occurred());
}

void test_syslog_handler_writes_process_message_to_kernel_logger(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    char message[] = "virtio_blk: probe failed\n";

    match_result(SystemCallResult, Syslog__handler(
        NULL, Some(ProcessPtr, process), (Anything) message, 0, 0, 0, 0
    ), success(code) { TEST_ASSERT_EQUAL(SYSLOG_OK, code); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "SYSLOG"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "virtio_blk: probe failed"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "hello_world"));
}

void test_syslog_handler_writes_tagged_process_message_to_kernel_logger(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);
    char tag[] = "VIRTIO-BLK";
    char message[] = "probe failed\n";

    match_result(SystemCallResult, Syslog__handler(
        NULL, Some(ProcessPtr, process), (Anything) message, (Anything) tag, 0, 0, 0
    ), success(code) { TEST_ASSERT_EQUAL(SYSLOG_OK, code); }, error(code, message) { TEST_FAIL(); })

    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "VIRTIO-BLK"));
    TEST_ASSERT_NULL(strstr(BasicIO__output(), "SYSLOG"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "probe failed"));
    TEST_ASSERT_NOT_NULL(strstr(BasicIO__output(), "hello_world"));
}

void test_syslog_handler_rejects_missing_message(void)
{
    ProcessPtr process = dummy_process(1, 0, PROC_STATE_RUNNING);

    match_result(SystemCallResult, Syslog__handler(
        NULL, Some(ProcessPtr, process), 0, 0, 0, 0, 0
    ), success(code) { TEST_FAIL(); }, error(code, message) { TEST_ASSERT_EQUAL(SYSLOG_INVALID_ARGUMENT, code); })
}

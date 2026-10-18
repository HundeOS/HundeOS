#include <string.h>

#include "unity.h"

#include "hunde/host_component.h"

static const uint8_t error_message[] = "component failed";

void test_status_result_distinguishes_success_from_error(void)
{
    HundeStatusResult success = HundeStatusResult__success();
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS, success.kind);

    HundeByteSpan message = HundeByteSpan__new(error_message, sizeof(error_message) - 1);
    HundeStatusResult error = HundeStatusResult__error(-42, message);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_ERROR, error.kind);
    TEST_ASSERT_EQUAL_INT32(-42, error.error.code);
    TEST_ASSERT_EQUAL_UINT64(sizeof(error_message) - 1, error.error.message.length);
    TEST_ASSERT_EQUAL_MEMORY(error_message, error.error.message.bytes, sizeof(error_message) - 1);
}

void test_component_load_result_carries_descriptor_or_error(void)
{
    HundeComponentDescriptorV1 descriptor = {
        .header = {.size = sizeof(HundeComponentDescriptorV1), .version = HUNDE_HOST_COMPONENT_ABI_VERSION},
    };
    HundeComponentLoadResult success = HundeComponentLoadResult__success(descriptor);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_SUCCESS, success.kind);
    TEST_ASSERT_EQUAL_UINT32(sizeof(HundeComponentDescriptorV1), success.component.header.size);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_HOST_COMPONENT_ABI_VERSION, success.component.header.version);

    HundeByteSpan message = HundeByteSpan__new(error_message, sizeof(error_message) - 1);
    HundeComponentLoadResult error = HundeComponentLoadResult__error(-7, message);
    TEST_ASSERT_EQUAL_UINT32(HUNDE_ABI_ERROR, error.kind);
    TEST_ASSERT_EQUAL_INT32(-7, error.error.code);
    TEST_ASSERT_EQUAL_MEMORY(error_message, error.error.message.bytes, sizeof(error_message) - 1);
}

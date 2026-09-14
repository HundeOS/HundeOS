#include "unity.h"

#include "services/service_directory.h"

void setUp(void)
{
}

void tearDown(void)
{
}

void test_directory_size_is_page_aligned_and_spans_multiple_pages(void)
{
    TEST_ASSERT_EQUAL_UINT64(1024, HUND_SERVICE_MAX_SERVICES);
    TEST_ASSERT_EQUAL_UINT64(0, HUND_SERVICE_DIRECTORY_SIZE % HUND_SERVICE_DIRECTORY_PAGE_SIZE);
    TEST_ASSERT_GREATER_THAN_UINT64(HUND_SERVICE_DIRECTORY_PAGE_SIZE, HUND_SERVICE_DIRECTORY_SIZE);
    TEST_ASSERT_GREATER_OR_EQUAL_UINT64(sizeof(ServiceDirectory), HUND_SERVICE_DIRECTORY_SIZE);
}

void test_directory_queries_reach_the_last_of_1024_entries(void)
{
    static ServiceDirectory directory;
    directory = (ServiceDirectory){0};
    directory.entries[1023] =
        (ServiceDirectoryEntry){.present = 1, .generation = 9999, .service_pid = 2345, .name = bstr("last-service")};
    match_optional(
        ServiceDirectory__find_endpoint(&directory, &bstr("last-service")),
        some(endpoint) { TEST_ASSERT_EQUAL_UINT64(9999, endpoint); },
        none_test_fail()
    );
    match_optional(
        ServiceDirectory__find_service_pid(&directory, &bstr("last-service")),
        some(pid) { TEST_ASSERT_EQUAL_UINT64(2345, pid); },
        none_test_fail()
    );
}

void test_directory_queries_return_optional_endpoint_and_service_pid(void)
{
    ServiceDirectory directory = {0};
    directory.entries[0].present = 1;
    directory.entries[0].generation = 41;
    directory.entries[0].service_pid = 9;
    directory.entries[0].name = bstr("protected");

    match_optional(
        ServiceDirectory__find_endpoint(&directory, &bstr("protected")),
        some(endpoint) { TEST_ASSERT_EQUAL_UINT64(41, endpoint); },
        none_test_fail()
    );
    match_optional(
        ServiceDirectory__find_service_pid(&directory, &bstr("protected")),
        some(pid) { TEST_ASSERT_EQUAL_UINT64(9, pid); },
        none_test_fail()
    );
    match_optional(ServiceDirectory__find_endpoint(&directory, &bstr("missing")),
        some_test_fail(),
        none_do_nothing()
    );
}

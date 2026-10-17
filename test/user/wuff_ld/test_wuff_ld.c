#include "unity.h"

#include "../../../user/lib/wuff/filesystem.h"
#include "../../../user/programs/wuff-ld/interpreter.h"

#define TEST_ENTRY 0x12345000ULL

static BString load_failed = bstr("Test image load failed");
static BString graph_failed = bstr("Test graph load failed");
static BString entry_failed = bstr("Test entry load failed");
static BString dispose_failed = bstr("Test graph disposal failed");
static uint8_t image_data[] = {1, 2, 3};
static BString loaded_path;
static BString graph_root_name;
static size_t image_release_calls;
static size_t graph_load_calls;
static size_t graph_entry_calls;
static size_t graph_dispose_calls;
static bool fail_image_load;
static bool fail_graph_load;
static bool fail_graph_entry;
static bool fail_graph_dispose;

Result(WuffImage) WuffFilesystem__load_image(ConstBStringPtr path)
{
    loaded_path = *path;
    if (fail_image_load)
    {
        return Error(WuffImage, load_failed, WUFF_LOAD_IO_FAILED);
    }
    WuffImage image = {
        .name = *path,
        .data = image_data,
        .size = sizeof(image_data),
    };
    return Success(WuffImage, image);
}

void WuffFilesystem__release_image(ConstWuffImagePtr image)
{
    image_release_calls++;
}

WuffDependencyOperations WuffFilesystem__dependencies(void)
{
    return (WuffDependencyOperations){0};
}

WuffMemoryOperations WuffMemoryOperations__system(void)
{
    return (WuffMemoryOperations){0};
}

Result(WuffGraph) WuffLoader__load_graph(ConstWuffImagePtr root, ConstWuffMemoryOperationsPtr memory,
                                         ConstWuffDependencyOperationsPtr dependencies)
{
    graph_load_calls++;
    graph_root_name = root->name;
    if (fail_graph_load)
    {
        return Error(WuffGraph, graph_failed, WUFF_LOAD_INVALID_IMAGE);
    }
    return Success(WuffGraph, ((WuffGraph){0}));
}

Result(Address) WuffGraph__entry(ConstWuffGraphPtr self)
{
    graph_entry_calls++;
    if (fail_graph_entry)
    {
        return Error(Address, entry_failed, WUFF_LOAD_INVALID_IMAGE);
    }
    return Success(Address, TEST_ENTRY);
}

Result(WuffLoadCode) WuffGraph__dispose(WuffGraphPtr self)
{
    graph_dispose_calls++;
    if (fail_graph_dispose)
    {
        return Error(WuffLoadCode, dispose_failed, WUFF_LOAD_CLEANUP_FAILED);
    }
    return Success(WuffLoadCode, WUFF_LOAD_OK);
}

void setUp(void)
{
    loaded_path = empty_bstr();
    graph_root_name = empty_bstr();
    image_release_calls = 0;
    graph_load_calls = 0;
    graph_entry_calls = 0;
    graph_dispose_calls = 0;
    fail_image_load = false;
    fail_graph_load = false;
    fail_graph_entry = false;
    fail_graph_dispose = false;
}

void tearDown(void)
{
}

void test_wuff_interpreter_loads_root_and_returns_owned_graph(void)
{
    BString path = bstr("/bin/program");

    match_result(
        WuffInterpreter__load(&path),
        success(program)
        {
            TEST_ASSERT_EQUAL_BSTRING(path, loaded_path);
            TEST_ASSERT_EQUAL_BSTRING(path, graph_root_name);
            TEST_ASSERT_EQUAL_HEX64(TEST_ENTRY, WuffInterpreterProgram__entry(&program));
            TEST_ASSERT_NOT_NULL(WuffInterpreterProgram__graph(&program));
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, image_release_calls);
    TEST_ASSERT_EQUAL(1, graph_load_calls);
    TEST_ASSERT_EQUAL(1, graph_entry_calls);
    TEST_ASSERT_EQUAL(0, graph_dispose_calls);
}

void test_wuff_interpreter_propagates_root_load_failure(void)
{
    BString path = bstr("/bin/program");
    fail_image_load = true;

    match_result(
        WuffInterpreter__load(&path),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_IO_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL(0, image_release_calls);
    TEST_ASSERT_EQUAL(0, graph_load_calls);
}

void test_wuff_interpreter_releases_root_after_graph_failure(void)
{
    BString path = bstr("/bin/program");
    fail_graph_load = true;

    match_result(
        WuffInterpreter__load(&path),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(1, image_release_calls);
    TEST_ASSERT_EQUAL(0, graph_entry_calls);
    TEST_ASSERT_EQUAL(0, graph_dispose_calls);
}

void test_wuff_interpreter_disposes_graph_without_entry(void)
{
    BString path = bstr("/bin/program");
    fail_graph_entry = true;

    match_result(
        WuffInterpreter__load(&path),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(1, image_release_calls);
    TEST_ASSERT_EQUAL(1, graph_dispose_calls);
}

void test_wuff_interpreter_reports_entry_cleanup_failure(void)
{
    BString path = bstr("/bin/program");
    fail_graph_entry = true;
    fail_graph_dispose = true;

    match_result(
        WuffInterpreter__load(&path),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_CLEANUP_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL(1, graph_dispose_calls);
}

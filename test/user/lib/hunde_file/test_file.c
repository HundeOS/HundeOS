#include <string.h>
#include "unity.h"
#include "hunde/native.h"
#include "sys/io.h"
#include "wuff/archive.h"

static BString contents;
static size_t offset;
static size_t reads;
static size_t closes;
static IOStatus open_error;
static IOStatus read_error;
static IOStatus close_error;
static IOStatus archive_error;
static IOStatus image_error;

void setUp(void)
{
    contents = bstr("manifest");
    offset = 0;
    reads = 0;
    closes = 0;
    open_error = IO_OK;
    read_error = IO_OK;
    close_error = IO_OK;
    archive_error = image_error = IO_OK;
}

void tearDown(void)
{
}

Result(EmbeddedFs) WuffArchive__mapped(void)
{
    if (archive_error != IO_OK)
    {
        return Error(EmbeddedFs, bstr("Invalid archive"), archive_error);
    }
    return Success(EmbeddedFs, EmbeddedFs__new(1, 2));
}

Result(WuffImage) WuffArchive__load_image(EmbeddedFsPtr archive, ConstBStringPtr path)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/config/bootstrap.fstab"), *path);
    if (image_error != IO_OK)
    {
        return Error(WuffImage, bstr("Image missing"), image_error);
    }
    return Success(WuffImage,
                   WuffImage__new(path, (const uint8_t *)BString__cstr(&contents), BString__length(&contents)));
}

WuffImage WuffImage__new(ConstBStringPtr name, const uint8_t * data, size_t size)
{
    return (WuffImage){.name = *name, .data = data, .size = size};
}

static HundeIOCompletion read_bootstrap(uint8_t * buffer, size_t capacity)
{
    static const uint8_t path[] = "/sbin/config/bootstrap.fstab";
    return hunde_rust_bootstrap_read(HundeByteSpan__new(path, sizeof(path) - 1), buffer, capacity);
}

void test_bootstrap_read_copies_archive_bytes_without_opening_any_file(void)
{
    uint8_t buffer[8];
    HundeIOCompletion result = read_bootstrap(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(8, result.value);
    TEST_ASSERT_EQUAL_MEMORY("manifest", buffer, 8);
    TEST_ASSERT_EQUAL(0, reads);
    TEST_ASSERT_EQUAL(0, closes);
}

void test_bootstrap_read_rejects_oversized_missing_or_invalid_archive_entries(void)
{
    uint8_t buffer[8];
    memset(buffer, 0x55, sizeof(buffer));
    HundeIOCompletion result = read_bootstrap(buffer, 7);
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, result.error.code);
    TEST_ASSERT_EACH_EQUAL_HEX8(0x55, buffer, sizeof(buffer));
    image_error = IO_INVALID_PATH;
    result = read_bootstrap(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, result.error.code);
    archive_error = IO_PERMISSION_DENIED;
    result = read_bootstrap(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_PERMISSION_DENIED, result.error.code);
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    TEST_ASSERT_EQUAL_BSTRING(bstr("/sbin/config/io-host.conf"), *path);
    TEST_ASSERT_EQUAL(IO_OPEN_READ, flags);
    if (open_error != IO_OK)
    {
        return Error(IOHandle, bstr("Open failed"), open_error);
    }
    return Success(IOHandle, 17);
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buffer, size_t size)
{
    TEST_ASSERT_EQUAL(17, handle);
    reads++;
    if (read_error != IO_OK)
    {
        return Error(ByteCount, bstr("Read failed"), read_error);
    }
    size_t remaining = BString__length(&contents) - offset;
    size_t count = remaining < size ? remaining : size;
    if (count > 3)
    {
        count = 3;
    }
    memcpy(buffer, BString__cstr(&contents) + offset, count);
    offset += count;
    return Success(ByteCount, count);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(17, handle);
    closes++;
    if (close_error != IO_OK)
    {
        return Error(IOStatus, bstr("Close failed"), close_error);
    }
    return Success(IOStatus, IO_OK);
}

static HundeIOCompletion read_config(uint8_t * buffer, size_t capacity)
{
    static const char path[] = "/sbin/config/io-host.conf";
    return hunde_rust_read_file((HundeByteSpan){.bytes = (const uint8_t *)path, .length = sizeof(path) - 1}, buffer,
                                capacity);
}

void test_bounded_read_collects_partial_reads_and_closes_on_success(void)
{
    uint8_t buffer[32];
    HundeIOCompletion result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(8, result.value);
    TEST_ASSERT_EQUAL_MEMORY("manifest", buffer, 8);
    TEST_ASSERT_GREATER_THAN(1, reads);
    TEST_ASSERT_EQUAL(1, closes);
}

void test_exact_limit_and_empty_files_are_accepted(void)
{
    uint8_t buffer[8];
    HundeIOCompletion result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(8, result.value);
    TEST_ASSERT_EQUAL(1, closes);
    contents = empty_bstr();
    offset = 0;
    result = read_config(buffer, 0);
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(0, result.value);
    TEST_ASSERT_EQUAL(2, closes);
}

void test_oversized_files_are_rejected_and_closed(void)
{
    uint8_t buffer[7];
    HundeIOCompletion result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_UNKNOWN_BUFFER, result.error.code);
    TEST_ASSERT_EQUAL(1, closes);
}

void test_open_read_and_close_errors_are_preserved(void)
{
    uint8_t buffer[32];
    open_error = IO_INVALID_PATH;
    HundeIOCompletion result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, result.error.code);
    TEST_ASSERT_EQUAL(0, reads);
    TEST_ASSERT_EQUAL(0, closes);
    open_error = IO_OK;
    read_error = IO_INTERRUPTED;
    result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(IO_INTERRUPTED, result.error.code);
    TEST_ASSERT_EQUAL(1, closes);
    read_error = IO_OK;
    close_error = IO_NOT_OPEN;
    result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(IO_NOT_OPEN, result.error.code);
    TEST_ASSERT_EQUAL(2, closes);
}

void test_eof_errors_are_normal_end_of_file_and_read_errors_survive_close_errors(void)
{
    uint8_t buffer[8];
    read_error = IO_EOF;
    HundeIOCompletion result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_SUCCESS, result.kind);
    TEST_ASSERT_EQUAL(0, result.value);
    TEST_ASSERT_EQUAL(1, closes);
    read_error = IO_INTERRUPTED;
    close_error = IO_NOT_OPEN;
    result = read_config(buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_INTERRUPTED, result.error.code);
    TEST_ASSERT_EQUAL(2, closes);
}

void test_invalid_path_is_rejected_before_open(void)
{
    uint8_t buffer[8];
    HundeIOCompletion result = hunde_rust_read_file(HundeByteSpan__new(buffer, 0), buffer, sizeof(buffer));
    TEST_ASSERT_EQUAL(HUNDE_ABI_ERROR, result.kind);
    TEST_ASSERT_EQUAL(IO_INVALID_PATH, result.error.code);
    TEST_ASSERT_EQUAL(0, reads);
    TEST_ASSERT_EQUAL(0, closes);
}

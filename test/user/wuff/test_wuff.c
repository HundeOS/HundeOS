#include <stdlib.h>
#include <string.h>

#include "unity.h"

#include "../../../user/lib/wuff/arch.h"
#include "../../../user/lib/wuff/filesystem.h"
#include "../../../user/lib/wuff/loader.h"
#include "../../../user/lib/sys/io.h"
#include "../../../user/lib/sys/mem.h"

#define TEST_IMAGE_SIZE 0x3000
#define TEST_BASE_ADDRESS 0x50000000ULL
#define TEST_DYNAMIC_OFFSET 0x1100
#define TEST_HASH_OFFSET 0x1200
#define TEST_SYMBOL_OFFSET 0x1240
#define TEST_STRING_OFFSET 0x12a0
#define TEST_RELOCATION_OFFSET 0x12c0
#define TEST_RELOCATION_TARGET_OFFSET 0x1300
#define TEST_JUMP_RELOCATION_OFFSET 0x12e0
#define TEST_JUMP_RELOCATION_TARGET_OFFSET 0x1380
#define TEST_INTERPRETER_OFFSET 0x380
#define TEST_OBJECT_STRIDE 0x10000
#define TEST_RELATIVE_RELOCATION 1027
#define TEST_ABSOLUTE_RELOCATION 257
#define TEST_JUMP_SLOT_RELOCATION 1026

typedef struct TestAllocation
{
    Address virtual_address;
    uint8_t * writable_address;
    size_t size;
    MemPagePermissions permissions;
} TestAllocation;

static bool TestAllocation__equals(TestAllocation * self, TestAllocation * other)
{
    return self->virtual_address == other->virtual_address;
}

OptionalDef(TestAllocation);
FixedArrayDefine(TestAllocation, 64, TestAllocations, (EqualFunction)TestAllocation__equals);
FixedArrayBodyDefine(TestAllocations);

typedef struct TestDependency
{
    WuffImage image;
} TestDependency;

static bool TestDependency__equals(TestDependency * self, TestDependency * other)
{
    return BString__equals(&self->image.name, &other->image.name);
}

OptionalDef(TestDependency);
FixedArrayDefine(TestDependency, 4, TestDependencies, (EqualFunction)TestDependency__equals);
FixedArrayBodyDefine(TestDependencies);

typedef struct TestProvider
{
    TestDependencies dependencies;
    size_t calls;
    size_t release_calls;
} TestProvider;

typedef struct TestMemory
{
    TestAllocations allocations;
    TestAllocations protections;
    size_t allocation_calls;
    size_t aligned_allocation_calls;
    size_t protection_calls;
    size_t free_calls;
    size_t fail_protection_call;
    bool fail_free;
    Address last_protection_address;
    size_t last_protection_size;
    MemPagePermissions last_protection_permissions;
} TestMemory;

static BString allocation_failed = bstr("Test allocation failed");
static BString dependency_missing = bstr("Test dependency missing");
static BString filesystem_last_path;
static const uint8_t * filesystem_data;
static size_t filesystem_size;
static size_t filesystem_offset;
static size_t filesystem_chunk_size;
static size_t filesystem_info_calls;
static size_t filesystem_open_calls;
static size_t filesystem_close_calls;
static IOStatus filesystem_info_status;
static IOStatus filesystem_close_status;
static IOHandle filesystem_handle;
static FileMode filesystem_mode;

Result(Anything) mem_alloc_bytes(size_t size)
{
    return Success(Anything, (Anything)malloc(size));
}

Result(IOFileInfo) IO__file_info(ConstBStringPtr path)
{
    filesystem_info_calls++;
    filesystem_last_path = *path;
    if (filesystem_info_status != IO_OK)
    {
        return Error(IOFileInfo, dependency_missing, filesystem_info_status);
    }
    return Success(IOFileInfo, IOFileInfo__new(filesystem_mode, 0, 0, filesystem_size));
}

Result(IOHandle) IO__open(ConstBStringPtr path, Flags flags)
{
    filesystem_open_calls++;
    filesystem_last_path = *path;
    TEST_ASSERT_EQUAL(IO_OPEN_READ, flags);
    return filesystem_handle < 0 ? Error(IOHandle, bstr("open failed"), filesystem_handle)
                                 : Success(IOHandle, filesystem_handle);
}

Result(ByteCount) IO__read_bytes(IOHandle handle, char * buffer, size_t size)
{
    TEST_ASSERT_EQUAL(filesystem_handle, handle);
    if (filesystem_offset >= filesystem_size)
    {
        return Error(ByteCount, bstr("end of file"), IO_EOF);
    }
    size_t remaining = filesystem_size - filesystem_offset;
    size_t count = remaining < size ? remaining : size;
    if (filesystem_chunk_size != 0 && count > filesystem_chunk_size)
    {
        count = filesystem_chunk_size;
    }
    memcpy(buffer, filesystem_data + filesystem_offset, count);
    filesystem_offset += count;
    return Success(ByteCount, (ByteCount)count);
}

Result(IOStatus) IO__close(IOHandle handle)
{
    TEST_ASSERT_EQUAL(filesystem_handle, handle);
    filesystem_close_calls++;
    return filesystem_close_status == IO_OK ? Success(IOStatus, IO_OK)
                                            : Error(IOStatus, bstr("close failed"), filesystem_close_status);
}

static void TestFilesystem__reset(const uint8_t * data, size_t size)
{
    filesystem_last_path = empty_bstr();
    filesystem_data = data;
    filesystem_size = size;
    filesystem_offset = 0;
    filesystem_chunk_size = 0;
    filesystem_info_calls = 0;
    filesystem_open_calls = 0;
    filesystem_close_calls = 0;
    filesystem_info_status = IO_OK;
    filesystem_close_status = IO_OK;
    filesystem_handle = 42;
    filesystem_mode = IO_MODE_TYPE_FILE | 0555;
}

static bool TestDependency__name_matches(TestDependency * dependency, void * context)
{
    return BString__equals(&dependency->image.name, context);
}

static Result(WuffImage) TestProvider__load(void * context, ConstBStringPtr name)
{
    TestProvider * self = context;
    self->calls++;
    match_find_result_ptr(
        TestDependencies__find(&self->dependencies, TestDependency__name_matches, (void *)name),
        some(dependency)
        {
            return Success(WuffImage, dependency->image);
        },
        none_do_nothing()
    );
    return Error(WuffImage, dependency_missing, WUFF_LOAD_DEPENDENCY_NOT_FOUND);
}

static void TestProvider__release(void * context, ConstWuffImagePtr image)
{
    TestProvider * self = context;
    self->release_calls++;
}

static void TestProvider__add(TestProvider * self, ConstBStringPtr name, const uint8_t * image)
{
    TestDependency dependency = {
        .image = WuffImage__new(name, image, TEST_IMAGE_SIZE),
    };
    match_result(TestDependencies__push(&self->dependencies, dependency),
        success_do_nothing(),
        error_test_fail()
    );
}

static bool TestAllocation__overlaps(TestAllocation * self, void * context)
{
    TestAllocation * candidate = context;
    return self->virtual_address < candidate->virtual_address + candidate->size &&
           candidate->virtual_address < self->virtual_address + self->size;
}

static Result(WuffMemoryRegion) TestMemory__allocate(TestMemory * self, Address address, size_t size)
{
    TestAllocation candidate = {.virtual_address = address, .size = size};
    if (is_something(TestAllocations__find(&self->allocations, TestAllocation__overlaps, &candidate)))
    {
        return Error(WuffMemoryRegion, bstr("Address already allocated"), MEM_INVALID_ADDRESS);
    }
    uint8_t * writable_address = calloc(1, size);
    if (writable_address == 0)
    {
        return Error(WuffMemoryRegion, allocation_failed, WUFF_LOAD_NO_MEMORY);
    }
    TestAllocation allocation = {
        .virtual_address = address,
        .writable_address = writable_address,
        .size = size,
        .permissions = MEM_PAGE_READ_WRITE,
    };
    match_result(
        TestAllocations__push(&self->allocations, allocation),
        success_do_nothing(),
        error(code, message)
        {
            free(writable_address);
            return Error(WuffMemoryRegion, message, code);
        }
    );
    self->allocation_calls++;
    return Success(WuffMemoryRegion, ((WuffMemoryRegion){
                                         .virtual_address = address,
                                         .writable_address = writable_address,
                                     }));
}

static Result(WuffMemoryRegion) TestMemory__allocate_aligned(void * context, size_t size, size_t alignment)
{
    TestMemory * self = context;
    Address object_base = TEST_BASE_ADDRESS + self->aligned_allocation_calls * TEST_OBJECT_STRIDE;
    self->aligned_allocation_calls++;
    Address address = (object_base + alignment - 1) & ~(alignment - 1);
    TestAllocation candidate = {.virtual_address = address, .size = size};
    for (;;)
    {
        match_find_result_ptr(
            TestAllocations__find(&self->allocations, TestAllocation__overlaps, &candidate),
            some(conflict)
            {
                candidate.virtual_address =
                    (conflict->virtual_address + conflict->size + alignment - 1) & ~(alignment - 1);
            },
            none()
            {
                return TestMemory__allocate(self, candidate.virtual_address, size);
            }
        );
    }
}

static MemStatus TestMemory__protect(void * context, Address address, size_t size, MemPagePermissions permissions)
{
    TestMemory * self = context;
    self->protection_calls++;
    self->last_protection_address = address;
    self->last_protection_size = size;
    self->last_protection_permissions = permissions;
    if (self->fail_protection_call == self->protection_calls)
    {
        return MEM_PERMISSION_DENIED;
    }
    for (size_t index = 0; index < TestAllocations__len(&self->allocations); index++)
    {
        match_optional(
            TestAllocations__ptr_at(&self->allocations, index),
            some(allocation)
            {
                if (address >= allocation->virtual_address && size <= allocation->size &&
                    address - allocation->virtual_address <= allocation->size - size)
                {
                    TestAllocation protection = ((TestAllocation){
                        .virtual_address = address,
                        .size = size,
                        .permissions = permissions,
                    });
                    match_result(
                        TestAllocations__push(&self->protections, protection),
                        success(value)
                        {
                            return MEM_OK;
                        },
                        error(code, message)
                        {
                            return MEM_ALLOCATION_FAILED;
                        }
                    );
                }
            },
            none_do_nothing()
        );
    }
    return MEM_INVALID_ADDRESS;
}

static MemStatus TestMemory__free(void * context, Address address)
{
    TestMemory * self = context;
    self->free_calls++;
    if (self->fail_free)
    {
        return MEM_FREE_FAILED;
    }
    for (size_t index = 0; index < TestAllocations__len(&self->allocations); index++)
    {
        match_optional(
            TestAllocations__at(&self->allocations, index),
            some(allocation)
            {
                if (allocation.virtual_address == address)
                {
                    free(allocation.writable_address);
                    match_result(TestAllocations__remove(&self->allocations, allocation),
                        success_do_nothing(),
                                 error_test_fail()
                    );
                    return MEM_OK;
                }
            },
            none_do_nothing()
        );
    }
    return MEM_INVALID_ADDRESS;
}

static WuffMemoryOperations TestMemory__operations(TestMemory * self)
{
    return (WuffMemoryOperations){
        .context = self,
        .allocate_aligned = TestMemory__allocate_aligned,
        .protect = TestMemory__protect,
        .free = TestMemory__free,
    };
}

static TestAllocation TestMemory__allocation(TestMemory * self, Address address)
{
    for (size_t index = 0; index < TestAllocations__len(&self->allocations); index++)
    {
        match_optional(
            TestAllocations__at(&self->allocations, index),
            some(allocation)
            {
                if (address >= allocation.virtual_address && address - allocation.virtual_address < allocation.size)
                {
                    allocation.writable_address += address - allocation.virtual_address;
                    allocation.size -= address - allocation.virtual_address;
                    allocation.virtual_address = address;
                    for_each_fa(
                        TestAllocations,
                        self->protections,
                        protection_index,
                        protection,
                        {
                            if (address >= protection.virtual_address &&
                                address - protection.virtual_address < protection.size)
                            {
                                allocation.permissions = protection.permissions;
                            }
                        }
                    );
                    return allocation;
                }
            },
            none_do_nothing()
        );
    }
    TEST_FAIL_MESSAGE("Test allocation not found");
    return (TestAllocation){0};
}

static void TestImage__write(uint8_t * image, size_t offset, const void * value, size_t size)
{
    TEST_ASSERT_TRUE(offset + size <= TEST_IMAGE_SIZE);
    memcpy(image + offset, value, size);
}

static void TestImage__dynamic_entry(uint8_t * image, size_t index, int64_t tag, uint64_t value)
{
    WuffDynamicEntry entry = {
        .tag = tag,
        .value = value,
    };
    TestImage__write(image, TEST_DYNAMIC_OFFSET + index * sizeof(entry), &entry, sizeof(entry));
}

static void TestImage__build(uint8_t * image)
{
    memset(image, 0, TEST_IMAGE_SIZE);
    WuffElfHeader header = {
        .identification = {0x7f, 'E', 'L', 'F', WUFF_ELF_CLASS_64, WUFF_ELF_LITTLE_ENDIAN, WUFF_ELF_CURRENT_VERSION},
        .type = WUFF_ELF_SHARED_OBJECT,
        .machine = WuffArch__machine(),
        .version = WUFF_ELF_CURRENT_VERSION,
        .program_header_offset = sizeof(WuffElfHeader),
        .header_size = sizeof(WuffElfHeader),
        .program_header_entry_size = sizeof(WuffProgramHeader),
        .program_header_count = 3,
    };
    WuffProgramHeader program_headers[] = {
        {
            .type = WUFF_PROGRAM_LOAD,
            .flags = WUFF_PROGRAM_READ | WUFF_PROGRAM_EXECUTE,
            .offset = 0,
            .virtual_address = 0,
            .file_size = 0x400,
            .memory_size = 0x800,
            .alignment = WUFF_PAGE_SIZE,
        },
        {
            .type = WUFF_PROGRAM_LOAD,
            .flags = WUFF_PROGRAM_READ | WUFF_PROGRAM_WRITE,
            .offset = 0x1000,
            .virtual_address = 0x2000,
            .file_size = 0x400,
            .memory_size = 0x2000,
            .alignment = WUFF_PAGE_SIZE,
        },
        {
            .type = WUFF_PROGRAM_DYNAMIC,
            .flags = WUFF_PROGRAM_READ | WUFF_PROGRAM_WRITE,
            .offset = TEST_DYNAMIC_OFFSET,
            .virtual_address = 0x2100,
            .file_size = sizeof(WuffDynamicEntry) * 13,
            .memory_size = sizeof(WuffDynamicEntry) * 13,
            .alignment = 8,
        },
    };
    TestImage__write(image, 0, &header, sizeof(header));
    TestImage__write(image, sizeof(header), program_headers, sizeof(program_headers));

    TestImage__dynamic_entry(image, 0, WUFF_DYNAMIC_HASH, 0x2200);
    TestImage__dynamic_entry(image, 1, WUFF_DYNAMIC_STRING_TABLE, 0x22a0);
    TestImage__dynamic_entry(image, 2, WUFF_DYNAMIC_SYMBOL_TABLE, 0x2240);
    TestImage__dynamic_entry(image, 3, WUFF_DYNAMIC_RELA, 0x22c0);
    TestImage__dynamic_entry(image, 4, WUFF_DYNAMIC_RELA_SIZE, sizeof(WuffRela));
    TestImage__dynamic_entry(image, 5, WUFF_DYNAMIC_RELA_ENTRY_SIZE, sizeof(WuffRela));
    TestImage__dynamic_entry(image, 6, WUFF_DYNAMIC_STRING_TABLE_SIZE, 10);
    TestImage__dynamic_entry(image, 7, WUFF_DYNAMIC_SYMBOL_ENTRY_SIZE, sizeof(WuffSymbolEntry));
    TestImage__dynamic_entry(image, 8, WUFF_DYNAMIC_PLT_RELOCATION_SIZE, 0);
    TestImage__dynamic_entry(image, 9, WUFF_DYNAMIC_PLT_RELOCATION_TYPE, WUFF_DYNAMIC_RELA);
    TestImage__dynamic_entry(image, 10, WUFF_DYNAMIC_JUMP_RELOCATION, 0);
    TestImage__dynamic_entry(image, 11, WUFF_DYNAMIC_NULL, 0);
    TestImage__dynamic_entry(image, 12, WUFF_DYNAMIC_NULL, 0);

    uint32_t hash[] = {1, 2, 1, 0, 0};
    TestImage__write(image, TEST_HASH_OFFSET, hash, sizeof(hash));
    WuffSymbolEntry symbols[] = {
        {0},
        {
            .name = 1,
            .information = WUFF_SYMBOL_GLOBAL << 4,
            .section_index = 1,
            .value = 0x300,
            .size = 4,
        },
    };
    TestImage__write(image, TEST_SYMBOL_OFFSET, symbols, sizeof(symbols));
    static const char strings[] = "\0wuff_add\0";
    TestImage__write(image, TEST_STRING_OFFSET, strings, sizeof(strings));
    WuffRela relocation = {
        .offset = 0x2300,
        .info = TEST_RELATIVE_RELOCATION,
        .addend = 0x300,
    };
    TestImage__write(image, TEST_RELOCATION_OFFSET, &relocation, sizeof(relocation));
}

static void TestImage__set_entry(uint8_t * image, Address entry)
{
    WuffElfHeader header;
    memcpy(&header, image, sizeof(header));
    header.entry = entry;
    TestImage__write(image, 0, &header, sizeof(header));
}

static void TestImage__add_bss_segment(uint8_t * image)
{
    WuffElfHeader header;
    memcpy(&header, image, sizeof(header));
    WuffProgramHeader segment = {
        .type = WUFF_PROGRAM_LOAD,
        .flags = WUFF_PROGRAM_READ | WUFF_PROGRAM_WRITE,
        .offset = TEST_IMAGE_SIZE,
        .virtual_address = 0x5000,
        .file_size = 0,
        .memory_size = WUFF_PAGE_SIZE,
        .alignment = WUFF_PAGE_SIZE,
    };
    TestImage__write(image, header.program_header_offset + header.program_header_count * sizeof(segment), &segment,
                     sizeof(segment));
    header.program_header_count++;
    TestImage__write(image, 0, &header, sizeof(header));
}

static void TestImage__add_interpreter(uint8_t * image, const char * path)
{
    WuffElfHeader header;
    memcpy(&header, image, sizeof(header));
    WuffProgramHeader interpreter = {
        .type = WUFF_PROGRAM_INTERPRETER,
        .flags = WUFF_PROGRAM_READ,
        .offset = TEST_INTERPRETER_OFFSET,
        .virtual_address = TEST_INTERPRETER_OFFSET,
        .file_size = strlen(path) + 1,
        .memory_size = strlen(path) + 1,
        .alignment = 1,
    };
    TestImage__write(image, header.program_header_offset + header.program_header_count * sizeof(interpreter),
                     &interpreter, sizeof(interpreter));
    header.program_header_count++;
    TestImage__write(image, TEST_INTERPRETER_OFFSET, path, interpreter.file_size);
    TestImage__write(image, 0, &header, sizeof(header));
}

static void TestImage__add_dependency(uint8_t * image, ConstBStringPtr name)
{
    size_t name_length = (size_t)BString__length(name);
    size_t string_offset = sizeof("\0wuff_add\0") - 1;
    TEST_ASSERT_TRUE(string_offset + name_length + 1 < 0x20);
    TestImage__write(image, TEST_STRING_OFFSET + string_offset, BString__cstr(name), name_length + 1);
    TestImage__dynamic_entry(image, 6, WUFF_DYNAMIC_STRING_TABLE_SIZE, string_offset + name_length + 1);
    TestImage__dynamic_entry(image, 11, WUFF_DYNAMIC_NEEDED, string_offset);
    TestImage__dynamic_entry(image, 12, WUFF_DYNAMIC_NULL, 0);
}

static void TestImage__import_symbol(uint8_t * image, uint8_t binding, int64_t addend)
{
    WuffSymbolEntry symbol = {
        .name = 1,
        .information = binding << 4,
        .section_index = WUFF_SYMBOL_UNDEFINED,
    };
    TestImage__write(image, TEST_SYMBOL_OFFSET + sizeof(WuffSymbolEntry), &symbol, sizeof(symbol));
    WuffRela relocation = {
        .offset = 0x2300,
        .info = ((uint64_t)1 << 32) | TEST_ABSOLUTE_RELOCATION,
        .addend = addend,
    };
    TestImage__write(image, TEST_RELOCATION_OFFSET, &relocation, sizeof(relocation));
}

void test_wuff_loader_maps_relocates_protects_and_finds_export(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TEST_ASSERT_EQUAL(1, TestAllocations__len(&memory.allocations));
            TEST_ASSERT_EQUAL(3, memory.protection_calls);
            TestAllocation code = TestMemory__allocation(&memory, TEST_BASE_ADDRESS);
            TestAllocation data = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x2000);
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_EXECUTE, code.permissions);
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_WRITE, data.permissions);
            Address relocated;
            memcpy(&relocated, data.writable_address + 0x300, sizeof(relocated));
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, relocated);
            BString name = bstr("wuff_add");
            match_optional(
                WuffObject__find_symbol(&object, &name),
                some(symbol)
                {
                    TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, symbol.address);
                    TEST_ASSERT_EQUAL(4, symbol.size);
                },
                none_test_fail()
            );
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_loader_reserves_the_whole_span_before_selecting_a_fragmented_hole(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestMemory memory = {.allocations = TestAllocations__new()};
    Address obstacle = TEST_BASE_ADDRESS + 0x2000;
    match_result(TestMemory__allocate(&memory, obstacle, 0x2000),
        success_do_nothing(),
        error_test_fail()
    );
    TestAllocation original = TestMemory__allocation(&memory, obstacle);
    memset(original.writable_address, 0xa5, original.size);
    WuffMemoryOperations operations = TestMemory__operations(&memory);
    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x4000, object.load_bias);
            TEST_ASSERT_EQUAL(1, memory.aligned_allocation_calls);
            TEST_ASSERT_EQUAL(2, memory.allocation_calls);
            TestAllocation image_allocation = TestMemory__allocation(&memory, object.load_bias);
            TEST_ASSERT_EQUAL_HEX64(0x4000, image_allocation.size);
            TestAllocation gap = TestMemory__allocation(&memory, object.load_bias + 0x1000);
            TEST_ASSERT_EQUAL(MEM_PAGE_NONE, gap.permissions);
            TEST_ASSERT_EACH_EQUAL_HEX8(0xa5, original.writable_address, original.size);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
            TEST_ASSERT_EQUAL(1, memory.free_calls);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(MEM_OK, TestMemory__free(&memory, obstacle));
}

void test_wuff_loader_uses_maximum_segment_alignment_and_handles_nonzero_first_page(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    WuffElfHeader header;
    memcpy(&header, image, sizeof(header));
    header.program_header_count = 2;
    TestImage__write(image, 0, &header, sizeof(header));
    WuffProgramHeader segment;
    memcpy(&segment, image + sizeof(header), sizeof(segment));
    segment.virtual_address = 0x1000;
    segment.offset = 0x1000;
    TestImage__write(image, sizeof(header), &segment, sizeof(segment));
    memcpy(&segment, image + sizeof(header) + sizeof(segment), sizeof(segment));
    segment.virtual_address = 0x10000;
    segment.offset = 0;
    segment.alignment = 0x10000;
    TestImage__write(image, sizeof(header) + sizeof(segment), &segment, sizeof(segment));
    TestMemory memory = {.allocations = TestAllocations__new()};
    match_result(TestMemory__allocate(&memory, TEST_BASE_ADDRESS, 0x1000),
        success_do_nothing(),
        error_test_fail()
    );
    WuffMemoryOperations operations = TestMemory__operations(&memory);
    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x10000, object.load_bias);
            TestAllocation leading_gap = TestMemory__allocation(&memory, object.load_bias);
            TEST_ASSERT_EQUAL_HEX64(0x12000, leading_gap.size);
            TEST_ASSERT_EQUAL(MEM_PAGE_NONE, leading_gap.permissions);
            TestAllocation code = TestMemory__allocation(&memory, object.load_bias + 0x1000);
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_EXECUTE, code.permissions);
            TestAllocation data = TestMemory__allocation(&memory, object.load_bias + 0x10000);
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_WRITE, data.permissions);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(MEM_OK, TestMemory__free(&memory, TEST_BASE_ADDRESS));
}

void test_wuff_loader_dispose_can_retry_after_failed_release(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);
    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            memory.fail_free = true;
            match_result(
                WuffObject__dispose(&object),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(WUFF_LOAD_CLEANUP_FAILED, code);
                }
            );
            TEST_ASSERT_EQUAL(1, TestAllocations__len(&memory.allocations));
            memory.fail_free = false;
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_loader_rejects_bad_magic_before_mapping(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    image[0] = 0;
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, memory.allocation_calls);
}

void test_wuff_loader_rolls_back_unsupported_relocation(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    WuffRela relocation;
    memcpy(&relocation, image + TEST_RELOCATION_OFFSET, sizeof(relocation));
    relocation.info = 0xffff;
    TestImage__write(image, TEST_RELOCATION_OFFSET, &relocation, sizeof(relocation));
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_UNSUPPORTED, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_loader_rolls_back_protection_failure(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestMemory memory = {
        .allocations = TestAllocations__new(),
        .fail_protection_call = 2,
    };
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_PROTECTION_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_loader_reports_cleanup_failure(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    WuffRela relocation;
    memcpy(&relocation, image + TEST_RELOCATION_OFFSET, sizeof(relocation));
    relocation.info = 0xffff;
    TestImage__write(image, TEST_RELOCATION_OFFSET, &relocation, sizeof(relocation));
    TestMemory memory = {
        .allocations = TestAllocations__new(),
        .fail_free = true,
    };
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_CLEANUP_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL(1, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
    memory.fail_free = false;
    TEST_ASSERT_EQUAL(MEM_OK, TestMemory__free(&memory, TEST_BASE_ADDRESS));
}

void test_wuff_loader_rejects_dependencies_until_dependency_loading_exists(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestImage__dynamic_entry(image, 11, WUFF_DYNAMIC_NEEDED, 1);
    TestImage__dynamic_entry(image, 12, WUFF_DYNAMIC_NULL, 0);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_UNSUPPORTED, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_loader_applies_defined_symbol_relocation(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    WuffRela relocation = {
        .offset = 0x2300,
        .info = ((uint64_t)1 << 32) | TEST_ABSOLUTE_RELOCATION,
        .addend = 4,
    };
    TestImage__write(image, TEST_RELOCATION_OFFSET, &relocation, sizeof(relocation));
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TestAllocation data = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x2000);
            Address relocated;
            memcpy(&relocated, data.writable_address + 0x300, sizeof(relocated));
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x304, relocated);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_wuff_loader_maps_zero_file_size_segment_as_zeroed_memory(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestImage__add_bss_segment(image);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TestAllocation bss = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x5000);
            uint8_t zeroes[WUFF_PAGE_SIZE] = {0};
            TEST_ASSERT_EQUAL_MEMORY(zeroes, bss.writable_address, sizeof(zeroes));
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_WRITE, bss.permissions);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_wuff_loader_applies_jump_slot_relocation(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestImage__dynamic_entry(image, 3, WUFF_DYNAMIC_STRING_TABLE_SIZE, 10);
    TestImage__dynamic_entry(image, 4, WUFF_DYNAMIC_SYMBOL_ENTRY_SIZE, sizeof(WuffSymbolEntry));
    TestImage__dynamic_entry(image, 5, WUFF_DYNAMIC_PLT_RELOCATION_SIZE, sizeof(WuffRela));
    TestImage__dynamic_entry(image, 6, WUFF_DYNAMIC_PLT_RELOCATION_TYPE, WUFF_DYNAMIC_RELA);
    TestImage__dynamic_entry(image, 7, WUFF_DYNAMIC_JUMP_RELOCATION, 0x22e0);
    TestImage__dynamic_entry(image, 8, WUFF_DYNAMIC_NULL, 0);
    WuffRela relocation = {
        .offset = 0x2380,
        .info = ((uint64_t)1 << 32) | TEST_JUMP_SLOT_RELOCATION,
    };
    TestImage__write(image, TEST_JUMP_RELOCATION_OFFSET, &relocation, sizeof(relocation));
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TestAllocation data = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x2000);
            Address relocated;
            memcpy(&relocated, data.writable_address + 0x380, sizeof(relocated));
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, relocated);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_wuff_loader_protects_relro_after_segment_permissions(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    WuffProgramHeader program_header;
    size_t offset = sizeof(WuffElfHeader) + 2 * sizeof(WuffProgramHeader);
    memcpy(&program_header, image + offset, sizeof(program_header));
    program_header.type = WUFF_PROGRAM_GNU_RELRO;
    program_header.virtual_address = 0x2100;
    program_header.memory_size = 0x800;
    TestImage__write(image, offset, &program_header, sizeof(program_header));
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success(object)
        {
            TEST_ASSERT_EQUAL(4, memory.protection_calls);
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x2000, memory.last_protection_address);
            TEST_ASSERT_EQUAL(WUFF_PAGE_SIZE, memory.last_protection_size);
            TEST_ASSERT_EQUAL(MEM_PAGE_READ_ONLY, memory.last_protection_permissions);
            match_result(WuffObject__dispose(&object),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_wuff_loader_rejects_unterminated_symbol_name(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    memset(image + TEST_STRING_OFFSET + 1, 'x', 9);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_graph_loads_transitive_dependencies_resolves_imports_and_deduplicates_cycle(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    uint8_t middle_image[TEST_IMAGE_SIZE];
    uint8_t leaf_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__build(middle_image);
    TestImage__build(leaf_image);
    BString root_name = bstr("root.wuff");
    BString middle_name = bstr("middle.wuff.1");
    BString leaf_name = bstr("leaf.wuff.1");
    TestImage__add_dependency(root_image, &middle_name);
    TestImage__import_symbol(root_image, WUFF_SYMBOL_GLOBAL, 4);
    TestImage__add_dependency(middle_image, &leaf_name);
    TestImage__import_symbol(middle_image, WUFF_SYMBOL_GLOBAL, 0);
    TestImage__add_dependency(leaf_image, &root_name);

    TestProvider provider = {.dependencies = TestDependencies__new()};
    TestProvider__add(&provider, &middle_name, middle_image);
    TestProvider__add(&provider, &leaf_name, leaf_image);
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            TEST_ASSERT_EQUAL(3, WuffLoadedObjects__len(&graph.objects));
            TEST_ASSERT_EQUAL(2, provider.calls);
            TEST_ASSERT_EQUAL(2, provider.release_calls);
            TestAllocation data = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x2000);
            Address relocated;
            memcpy(&relocated, data.writable_address + 0x300, sizeof(relocated));
            TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 2 * TEST_OBJECT_STRIDE + 0x304, relocated);
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(3, memory.free_calls);
}

void test_wuff_module_graph_finds_required_root_export(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    BString root_name = bstr("driver.wuff");
    BString export_name = bstr("wuff_add");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_module_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            match_result(
                WuffGraph__find_root_export(&graph, &export_name),
                success(symbol)
                {
                    TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, symbol.address);
                    TEST_ASSERT_EQUAL(4, symbol.size);
                },
                error_test_fail()
            );
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_module_graph_reports_missing_required_root_export(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    BString root_name = bstr("driver.wuff");
    BString export_name = bstr("missing_export");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_module_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            match_result(
                WuffGraph__find_root_export(&graph, &export_name),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(WUFF_LOAD_SYMBOL_NOT_FOUND, code);
                }
            );
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_module_graph_does_not_use_dependency_as_root_export(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    uint8_t dependency_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__build(dependency_image);
    TestImage__import_symbol(root_image, WUFF_SYMBOL_GLOBAL, 0);
    BString root_name = bstr("driver.wuff");
    BString dependency_name = bstr("dependency.wuff.1");
    BString export_name = bstr("wuff_add");
    TestImage__add_dependency(root_image, &dependency_name);
    TestProvider provider = {.dependencies = TestDependencies__new()};
    TestProvider__add(&provider, &dependency_name, dependency_image);
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_module_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            match_result(
                WuffGraph__find_root_export(&graph, &export_name),
                success_test_fail(),
                error(code, message)
                {
                    TEST_ASSERT_EQUAL(WUFF_LOAD_SYMBOL_NOT_FOUND, code);
                }
            );
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, provider.release_calls);
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_module_graph_rejects_program_interpreter(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__add_interpreter(root_image, WUFF_INTERPRETER_PATH);
    BString root_name = bstr("driver.wuff");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_module_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, memory.allocation_calls);
}

void test_wuff_graph_binds_unresolved_weak_symbol_to_zero(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__import_symbol(root_image, WUFF_SYMBOL_WEAK, 8);
    BString root_name = bstr("root.wuff");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            TestAllocation data = TestMemory__allocation(&memory, TEST_BASE_ADDRESS + 0x2000);
            Address relocated;
            memcpy(&relocated, data.writable_address + 0x300, sizeof(relocated));
            TEST_ASSERT_EQUAL_HEX64(8, relocated);
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(0, provider.calls);
}

void test_wuff_graph_rolls_back_all_objects_when_strong_import_is_missing(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__import_symbol(root_image, WUFF_SYMBOL_GLOBAL, 0);
    BString root_name = bstr("root.wuff");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_SYMBOL_NOT_FOUND, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_graph_rolls_back_root_when_dependency_provider_fails(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    BString root_name = bstr("root.wuff");
    BString missing_name = bstr("missing.wuff.1");
    TestImage__add_dependency(root_image, &missing_name);
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_DEPENDENCY_NOT_FOUND, code);
        }
    );
    TEST_ASSERT_EQUAL(1, provider.calls);
    TEST_ASSERT_EQUAL(0, provider.release_calls);
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_graph_releases_dependency_image_when_preparation_fails(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    uint8_t invalid_image[TEST_IMAGE_SIZE] = {0};
    TestImage__build(root_image);
    BString root_name = bstr("root.wuff");
    BString invalid_name = bstr("invalid.wuff.1");
    TestImage__add_dependency(root_image, &invalid_name);
    TestProvider provider = {.dependencies = TestDependencies__new()};
    TestProvider__add(&provider, &invalid_name, invalid_image);
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(1, provider.calls);
    TEST_ASSERT_EQUAL(1, provider.release_calls);
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_graph_reports_validated_root_entry(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__set_entry(root_image, 0x300);
    TestImage__add_interpreter(root_image, WUFF_INTERPRETER_PATH);
    BString root_name = bstr("root.wuff");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success(graph)
        {
            match_result(
                WuffGraph__entry(&graph),
                success(entry)
                {
                    TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, entry);
                },
                error_test_fail()
            );
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
}

void test_wuff_loader_rejects_interpreter_outside_graph_root(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestImage__add_interpreter(image, WUFF_INTERPRETER_PATH);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, memory.allocation_calls);
}

void test_wuff_graph_rejects_unexpected_root_interpreter(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__add_interpreter(root_image, "/sbin/other-ld");
    BString root_name = bstr("root.wuff");
    TestProvider provider = {.dependencies = TestDependencies__new()};
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, memory.allocation_calls);
}

void test_wuff_graph_rejects_interpreter_in_dependency_and_releases_image(void)
{
    uint8_t root_image[TEST_IMAGE_SIZE];
    uint8_t dependency_image[TEST_IMAGE_SIZE];
    TestImage__build(root_image);
    TestImage__build(dependency_image);
    TestImage__add_interpreter(dependency_image, WUFF_INTERPRETER_PATH);
    BString root_name = bstr("root.wuff");
    BString dependency_name = bstr("dependency.wuff.1");
    TestImage__add_dependency(root_image, &dependency_name);
    TestProvider provider = {.dependencies = TestDependencies__new()};
    TestProvider__add(&provider, &dependency_name, dependency_image);
    WuffDependencyOperations dependency_operations =
        WuffDependencyOperations__new(&provider, TestProvider__load, TestProvider__release);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);
    WuffImage root = WuffImage__new(&root_name, root_image, sizeof(root_image));

    match_result(
        WuffLoader__load_graph(&root, &memory_operations, &dependency_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(1, provider.release_calls);
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_loader_rejects_entry_outside_executable_mapping(void)
{
    uint8_t image[TEST_IMAGE_SIZE];
    TestImage__build(image);
    TestImage__set_entry(image, 0x2300);
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations operations = TestMemory__operations(&memory);

    match_result(
        WuffLoader__load(image, sizeof(image), &operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
    TEST_ASSERT_EQUAL(1, memory.free_calls);
}

void test_wuff_filesystem_loads_from_usr_lib_and_releases_image(void)
{
    static const uint8_t contents[] = {1, 2, 3, 4, 5};
    TestFilesystem__reset(contents, sizeof(contents));
    filesystem_chunk_size = 2;
    BString name = bstr("libc.wuff.1");
    WuffDependencyOperations dependencies = WuffFilesystem__dependencies();

    match_result(
        dependencies.load(dependencies.context, &name),
        success(image)
        {
            TEST_ASSERT_EQUAL_BSTRING(bstr("/usr/lib/libc.wuff.1"), filesystem_last_path);
            TEST_ASSERT_EQUAL_BSTRING(name, image.name);
            TEST_ASSERT_EQUAL(sizeof(contents), image.size);
            TEST_ASSERT_EQUAL_MEMORY(contents, image.data, sizeof(contents));
            dependencies.release(dependencies.context, &image);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, filesystem_info_calls);
    TEST_ASSERT_EQUAL(1, filesystem_open_calls);
    TEST_ASSERT_EQUAL(1, filesystem_close_calls);
}

void test_wuff_filesystem_loads_root_image_from_absolute_path(void)
{
    static const uint8_t contents[] = {5, 4, 3, 2, 1};
    TestFilesystem__reset(contents, sizeof(contents));
    BString path = bstr("/bin/program");

    match_result(
        WuffFilesystem__load_image(&path),
        success(image)
        {
            TEST_ASSERT_EQUAL_BSTRING(path, filesystem_last_path);
            TEST_ASSERT_EQUAL_BSTRING(path, image.name);
            TEST_ASSERT_EQUAL(sizeof(contents), image.size);
            TEST_ASSERT_EQUAL_MEMORY(contents, image.data, sizeof(contents));
            WuffFilesystem__release_image(&image);
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL(1, filesystem_info_calls);
    TEST_ASSERT_EQUAL(1, filesystem_open_calls);
    TEST_ASSERT_EQUAL(1, filesystem_close_calls);
}

void test_wuff_filesystem_loads_module_and_releases_root_image(void)
{
    uint8_t contents[TEST_IMAGE_SIZE];
    TestImage__build(contents);
    TestFilesystem__reset(contents, sizeof(contents));
    BString path = bstr("/usr/lib/hunde/drivers/block.wuff");
    BString export_name = bstr("wuff_add");
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);

    match_result(
        WuffFilesystem__load_module(&path, &memory_operations),
        success(graph)
        {
            match_result(
                WuffGraph__find_root_export(&graph, &export_name),
                success(symbol)
                {
                    TEST_ASSERT_EQUAL_HEX64(TEST_BASE_ADDRESS + 0x300, symbol.address);
                },
                error_test_fail()
            );
            match_result(WuffGraph__dispose(&graph),
                success_do_nothing(),
                error_test_fail()
            );
        },
        error_test_fail()
    );
    TEST_ASSERT_EQUAL_BSTRING(path, filesystem_last_path);
    TEST_ASSERT_EQUAL(1, filesystem_info_calls);
    TEST_ASSERT_EQUAL(1, filesystem_open_calls);
    TEST_ASSERT_EQUAL(1, filesystem_close_calls);
    TEST_ASSERT_EQUAL(0, TestAllocations__len(&memory.allocations));
}

void test_wuff_filesystem_releases_rejected_module_image(void)
{
    uint8_t contents[TEST_IMAGE_SIZE];
    TestImage__build(contents);
    TestImage__add_interpreter(contents, WUFF_INTERPRETER_PATH);
    TestFilesystem__reset(contents, sizeof(contents));
    BString path = bstr("/usr/lib/hunde/drivers/program.wuff");
    TestMemory memory = {.allocations = TestAllocations__new()};
    WuffMemoryOperations memory_operations = TestMemory__operations(&memory);

    match_result(
        WuffFilesystem__load_module(&path, &memory_operations),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_INVALID_IMAGE, code);
        }
    );
    TEST_ASSERT_EQUAL_BSTRING(path, filesystem_last_path);
    TEST_ASSERT_EQUAL(1, filesystem_info_calls);
    TEST_ASSERT_EQUAL(1, filesystem_open_calls);
    TEST_ASSERT_EQUAL(1, filesystem_close_calls);
    TEST_ASSERT_EQUAL(0, memory.allocation_calls);
}

void test_wuff_filesystem_rejects_dependency_paths(void)
{
    static const uint8_t contents[] = {1};
    TestFilesystem__reset(contents, sizeof(contents));
    BString name = bstr("../libc.wuff.1");
    WuffDependencyOperations dependencies = WuffFilesystem__dependencies();

    match_result(
        dependencies.load(dependencies.context, &name),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_DEPENDENCY_NOT_FOUND, code);
        }
    );
    TEST_ASSERT_EQUAL(0, filesystem_info_calls);
    TEST_ASSERT_EQUAL(0, filesystem_open_calls);
}

void test_wuff_filesystem_reports_close_failure(void)
{
    static const uint8_t contents[] = {1, 2, 3};
    TestFilesystem__reset(contents, sizeof(contents));
    filesystem_close_status = IO_UNKNOWN_HANDLE;
    BString name = bstr("libc.wuff.1");
    WuffDependencyOperations dependencies = WuffFilesystem__dependencies();

    match_result(
        dependencies.load(dependencies.context, &name),
        success_test_fail(),
        error(code, message)
        {
            TEST_ASSERT_EQUAL(WUFF_LOAD_IO_FAILED, code);
        }
    );
    TEST_ASSERT_EQUAL(1, filesystem_close_calls);
}

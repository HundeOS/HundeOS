#include "unity.h"
#include "../fixtures/kernel.h"
#include "../fixtures/process.h"
#include "../stubs/page_table.h"
#include "../stubs/kalloc.h"
#include "../../../kernel/service/device_registry.h"

static KernelPtr kernel;
static ProcessPtr process;
static DeviceRegistryPtr directory;

void setUp(void)
{
    kernel = init_test_kernel_without_filesystem();
    process = dummy_process(20, 0, PROC_STATE_READY);
    match_result(KernelDeviceRegistry__map_process(&kernel->device_registry, process),
        success_do_nothing(),
                 error_test_fail()
    );
    directory = k_v_ptr(kernel->device_registry.mapping, DeviceRegistry);
}

void tearDown(void)
{
    KAllocStub__set_user_alloc_failure(false);
}

void test_device_registry_allocation_failure_does_not_publish_a_mapping(void)
{
    KernelDeviceRegistry other = KernelDeviceRegistry__new();
    KAllocStub__set_user_alloc_failure(true);
    match_result(
        KernelDeviceRegistry__map_process(&other, process),
        success(code) { TEST_FAIL(); },
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_MAP_FAILED, code); }
    );
    TEST_ASSERT_FALSE(other.mapping.valid);
}

void test_full_device_registry_does_not_replace_an_existing_device(void)
{
    for (uint64_t index = 0; index < DEVICE_REGISTRY_CAPACITY; index++)
    {
        BString path = BString__sprintf(&bstr("/dev/disk%llu"), index);
        DeviceRecord record = DeviceRecord__new(DEVICE_BLOCK, 20, &path, &empty_bstr(), 512, 100);
        match_result(DeviceRegistry__set(directory, index, &record),
            success_do_nothing(),
            error_test_fail()
        );
    }
    DeviceRecord extra = DeviceRecord__new(DEVICE_BLOCK, 21, &bstr("/dev/extra"), &empty_bstr(), 512, 100);
    match_result(
        KernelDeviceRegistry__publish(&kernel->device_registry, &extra),
        success(code) { TEST_FAIL(); },
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_EVENT_QUEUE_FULL, code); }
    );
    match_result(
        DeviceRegistry__at(directory, 0),
        success(record) { TEST_ASSERT_EQUAL_BSTRING(bstr("/dev/disk0"), record.path); },
        error_test_fail()
    );
}

void test_device_registry_maps_read_only_and_non_executable(void)
{
    TEST_ASSERT_TRUE(check_address_mapped(ProcessPageTable__root_table(&process->page_table), DEVICE_REGISTRY_VADDR,
                                          true, false, false, DEVICE_REGISTRY_SIZE));
    TEST_ASSERT_TRUE(kernel->device_registry.mapping.read);
    TEST_ASSERT_FALSE(kernel->device_registry.mapping.write);
    TEST_ASSERT_FALSE(kernel->device_registry.mapping.execute);
}

void test_device_registry_rejects_duplicates_and_removes_only_departed_owner(void)
{
    DeviceRecord record = DeviceRecord__new(DEVICE_BLOCK, 20, &bstr("/dev/virtio1"), &bstr("hd0"), 512, 100);
    match_result(KernelDeviceRegistry__publish(&kernel->device_registry, &record),
        success_do_nothing(),
                 error_test_fail()
    );
    match_result(
        KernelDeviceRegistry__publish(&kernel->device_registry, &record),
        success(code) { TEST_FAIL(); },
        error(code, message) { TEST_ASSERT_EQUAL(SERVICE_DEVICE_ALREADY_REGISTERED, code); }
    );
    KernelDeviceRegistry__remove_process(&kernel->device_registry, 21);
    match_result(
        DeviceRegistry__find(directory, &bstr("/dev/hd0")),
        success(found)
        {
            match_optional(found,
                some(record) { TEST_ASSERT_EQUAL(20, record.owner_pid); },
                none_test_fail()
            );
        },
        error_test_fail()
    );
    KernelDeviceRegistry__remove_process(&kernel->device_registry, 20);
    KernelDeviceRegistry__remove_process(&kernel->device_registry, 20);
    match_result(
        DeviceRegistry__find(directory, &bstr("/dev/hd0")),
        success(found) { match_optional(found,
            some(record) { TEST_FAIL(); },
            none_do_nothing()
        ); },
        error_test_fail()
    );
    match_result(KernelDeviceRegistry__publish(&kernel->device_registry, &record),
        success_do_nothing(),
                 error_test_fail()
    );
}

void test_device_registry_bounds_and_invalid_version_fail_closed(void)
{
    match_result(
        DeviceRegistry__at(directory, DEVICE_REGISTRY_CAPACITY),
        success(record) { TEST_FAIL(); },
        error(code, message) { TEST_ASSERT_NOT_EQUAL(0, code); }
    );
    directory->version++;
    match_result(
        DeviceRegistry__at(directory, 0),
        success(record) { TEST_FAIL(); },
        error(code, message) { TEST_ASSERT_NOT_EQUAL(0, code); }
    );
}

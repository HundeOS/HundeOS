#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/process.h"
#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/spawn.h"
#include "../../../../common_includes/syscalls/syslog.h"

#define TEST_SERVICE_READY_NAME_MAX 256ULL

typedef struct SpawnCall
{
    SpawnMode mode;
    char path[64];
    char arg0[64];
    char arg1[64];
} SpawnCall;

struct ServiceMappedMemory
{
    uint64_t driver_address;
    uint64_t device_address;
    size_t size;
};

static SpawnCall spawn_calls[16];
static uint64_t spawn_call_count;
static const char * failing_path;
static uint8_t fake_virtio_region[0x200ULL * 32ULL];
static ServiceMappedMemory fake_mapping;
static ProcessChildEvent fake_child_events[4];
static uint64_t fake_child_event_count;
static uint64_t fake_child_event_index;
static const char * fake_sysinfo;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static const char * fake_config;
static bool fake_config_opened;
static bool fake_config_closed;
static uint64_t fake_wait_ready_count;
static char fake_wait_ready_paths[10][TEST_SERVICE_READY_NAME_MAX];

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

#define ServiceDirectory__shared SchoolTest__directory
#define HUNDESCHULE_ENTRYPOINT hundeschule_main_under_test
#define IO__printf test_printf
#include "../../../../user/services/hundeschule/main.c"
#undef IO__printf
#undef ServiceDirectory__shared
static ServiceDirectory school_directory;
ServiceDirectoryPtr SchoolTest__directory(void)
{
    return &school_directory;
}
ServiceCode service_publish_ready(ConstBStringPtr name)
{
    return SERVICE_OK;
}
Result(IOStatus) Hundeschule__mount_filesystems(void)
{
    return Success(IOStatus, IO_OK);
}

void setUp(void)
{
    spawn_call_count = 0;
    failing_path = NULL;
    for (uint64_t i = 0; i < 16; i++)
    {
        spawn_calls[i] = (SpawnCall){0};
    }

    fake_mapping = (ServiceMappedMemory){
        .driver_address = (uint64_t)fake_virtio_region,
        .device_address = 0x0a000000ULL,
        .size = sizeof(fake_virtio_region),
    };

    for (uint64_t i = 0; i < sizeof(fake_virtio_region); i++)
    {
        fake_virtio_region[i] = 0;
    }

    volatile uint32_t * slot0 = (volatile uint32_t *)&fake_virtio_region[0];
    volatile uint32_t * slot1 = (volatile uint32_t *)&fake_virtio_region[0x200];
    volatile uint32_t * slot2 = (volatile uint32_t *)&fake_virtio_region[0x400];

    slot0[0x000 / 4] = 0x74726976U;
    slot0[0x004 / 4] = 1;
    slot0[0x008 / 4] = 2;

    slot1[0x000 / 4] = 0x74726976U;
    slot1[0x004 / 4] = 1;
    slot1[0x008 / 4] = 0;

    slot2[0x000 / 4] = 0x74726976U;
    slot2[0x004 / 4] = 1;
    slot2[0x008 / 4] = 2;

    fake_child_event_count = 0;
    fake_child_event_index = 0;
    fake_sysinfo = "framebuffer.present=0\n";
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    fake_config = NULL;
    fake_config_opened = false;
    fake_config_closed = false;
    fake_wait_ready_count = 0;
    for (uint64_t i = 0; i < 10; i++)
    {
        fake_wait_ready_paths[i][0] = '\0';
    }
}

void tearDown(void)
{
}

static int strings_equal(const char * left, const char * right)
{
    uint64_t i = 0;
    while (left[i] != '\0' && right[i] != '\0')
    {
        if (left[i] != right[i])
        {
            return 0;
        }
        i++;
    }
    return left[i] == right[i];
}

static void copy_string(char * target, uint64_t size, const char * source)
{
    uint64_t i = 0;
    if (source != NULL)
    {
        while (i + 1 < size && source[i] != '\0')
        {
            target[i] = source[i];
            i++;
        }
    }
    target[i] = '\0';
}

SpawnResult spawn(SpawnMode mode, ConstBStringPtr path, OptionalPtr(BString) arg0, OptionalPtr(BString) arg1,
                  OptionalPtr(BString) arg2)
{
    TEST_ASSERT_LESS_THAN_UINT64(16, spawn_call_count);
    spawn_calls[spawn_call_count].mode = mode;
    copy_string(spawn_calls[spawn_call_count].path, sizeof(spawn_calls[spawn_call_count].path), BString__cstr(path));
    match_optional(
        *arg0,
        some(value)
        {
            copy_string(spawn_calls[spawn_call_count].arg0, sizeof(spawn_calls[spawn_call_count].arg0),
                        BString__cstr(&value));
        },
        none_do_nothing()
    );
    match_optional(
        *arg1,
        some(value)
        {
            copy_string(spawn_calls[spawn_call_count].arg1, sizeof(spawn_calls[spawn_call_count].arg1),
                        BString__cstr(&value));
        },
        none_do_nothing()
    );
    spawn_call_count++;

    if (failing_path != NULL && strings_equal(BString__cstr(path), failing_path))
    {
        return SPAWN_NO_FILE;
    }

    return (SpawnResult)spawn_call_count;
}

SpawnResult spawn_with_stdio(SpawnMode mode, ConstBStringPtr path, SpawnFileDescriptor stdin_fd,
                             SpawnFileDescriptor stdout_fd, SpawnFileDescriptor stderr_fd, OptionalPtr(BString) arg0,
                             OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_FAIL();
    return SPAWN_FAILED;
}

SpawnResult spawn_as(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path, OptionalPtr(BString) arg0,
                     OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_FAIL();
    return SPAWN_FAILED;
}

IOHandle open(ConstBStringPtr path, Flags flags)
{
    if (strings_equal(BString__cstr(path), "/boot/sysinfo"))
    {
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
        fake_sysinfo_opened = true;
        return 1;
    }

    if (strings_equal(BString__cstr(path), "/boot/config/hundeschule.conf"))
    {
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
        if (fake_wait_ready_count < 4 || fake_config == NULL)
        {
            return IO_INVALID_PATH;
        }
        fake_config_opened = true;
        return 2;
    }

    TEST_FAIL();
    return IO_INVALID_PATH;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, StringPos max_bytes)
{
    const char * source = fake_sysinfo;
    if (handle == 1)
    {
        TEST_ASSERT_TRUE(fake_sysinfo_opened);
    }
    else if (handle == 2)
    {
        TEST_ASSERT_TRUE(fake_config_opened);
        source = fake_config;
    }
    else
    {
        TEST_FAIL();
        return None(DynString);
    }

    DynString config;
    match_result(
        DynString__init(&config, max_bytes),
        success_do_nothing(),
        error(code, message)
        {
            return None(DynString);
        }
    );

    for (StringPos index = 0; source[index] != '\0' && index < max_bytes; index++)
    {
        match_result(
            DynString__push_char(&config, source[index]),
            success_do_nothing(),
            error(code, message)
            {
                DynString__dispose(&config);
                return None(DynString);
            }
        );
    }

    return Some(DynString, config);
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    TEST_FAIL();
    return IO_PANIC;
}

IOStatus close(IOHandle handle)
{
    if (handle == 1)
    {
        fake_sysinfo_closed = true;
    }
    else if (handle == 2)
    {
        fake_config_closed = true;
    }
    else
    {
        TEST_FAIL();
    }
    return IO_OK;
}

ServiceCode service_map_mmio(Address physical_address, size_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(0x0a000000ULL, physical_address);
    TEST_ASSERT_EQUAL_UINT64(sizeof(fake_virtio_region), size);
    *memory = &fake_mapping;
    return SERVICE_OK;
}

ServiceCode service_become_service()
{
    return SERVICE_OK;
}

ServiceCode service_wait_ready(ConstBStringPtr name)
{
    TEST_ASSERT_LESS_THAN_UINT64(10, fake_wait_ready_count);
    BString__copy_to(name, fake_wait_ready_paths[fake_wait_ready_count], TEST_SERVICE_READY_NAME_MAX);
    fake_wait_ready_count++;
    return SERVICE_OK;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (void *)(memory->driver_address + offset);
}

int test_printf(ConstBStringPtr format, ...)
{
    return 0;
}

ProcessChildEventResult wait_for_child_event(ProcessChildEventPtr event)
{
    if (fake_child_event_index >= fake_child_event_count)
    {
        return PROCESS_CHILD_EVENT_QUEUE_EMPTY;
    }

    *event = fake_child_events[fake_child_event_index++];
    return PROCESS_CHILD_EVENT_OK;
}

void test_hundeschule_starts_bootstrap_drivers_then_hundeschau(void)
{
    HundeschuleRequiredProcesses processes = HundeschuleRequiredProcesses__new();
    TEST_ASSERT_EQUAL(0, Hundeschule__initialise_required_processes(&processes));
    TEST_ASSERT_EQUAL(7, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("hunde-mama", fake_wait_ready_paths[0]);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/socket_router", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", spawn_calls[1].arg1);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", spawn_calls[2].arg1);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/hundebus", spawn_calls[3].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/uart_rx", spawn_calls[4].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/uart_tx", spawn_calls[5].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/hundeschau", spawn_calls[6].path);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
}

void test_hundeschule_uses_config_for_machine_bootstrap_drivers(void)
{
    fake_sysinfo = "framebuffer.present=1\n"
                   "framebuffer.physical_base=0x3e000000\n"
                   "framebuffer.physical_size=0x20000\n"
                   "framebuffer.width=512\n"
                   "framebuffer.height=64\n"
                   "framebuffer.pixels_per_scanline=512\n"
                   "framebuffer.pixel_format=0\n";
    fake_config = "# hundeschule driver config\n"
                  "driver /sbin/drivers/hundebus once\n"
                  "driver /sbin/services/hundekanal-test-echo once\n"
                  "driver /sbin/drivers/sdhci_channelio once\n"
                  "driver /sbin/drivers/virtio_keyboard respawn framebuffer\n"
                  "driver /sbin/drivers/uart_rx respawn no-framebuffer\n";

    HundeschuleRequiredProcesses required_processes = HundeschuleRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(0, Hundeschule__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(8, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/socket_router", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/hundebus", spawn_calls[3].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/hundekanal-test-echo", spawn_calls[4].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/sdhci_channelio", spawn_calls[5].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_keyboard", spawn_calls[6].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/hundeschau", spawn_calls[7].path);
    TEST_ASSERT_EQUAL_UINT64(4, fake_wait_ready_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", fake_wait_ready_paths[2]);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", fake_wait_ready_paths[3]);
    TEST_ASSERT_TRUE(fake_config_opened);
    TEST_ASSERT_TRUE(fake_config_closed);
}

void test_hundeschule_stops_before_disks_when_interrupt_manager_cannot_start(void)
{
    failing_path = "/sbin/drivers/socket_router";
    HundeschuleRequiredProcesses processes = HundeschuleRequiredProcesses__new();
    TEST_ASSERT_EQUAL(1, Hundeschule__initialise_required_processes(&processes));
    TEST_ASSERT_EQUAL(1, spawn_call_count);
    TEST_ASSERT_EQUAL(1, fake_wait_ready_count);
    TEST_ASSERT_FALSE(fake_config_opened);
}

void test_hundeschule_rejects_legacy_block_driver_selection(void)
{
    TEST_ASSERT_FALSE(Hundeschule__is_virtio_block_driver(&bstr("/sbin/drivers/virtio_blk")));
    TEST_ASSERT_TRUE(Hundeschule__is_virtio_block_driver(&bstr("/sbin/drivers/virtio_blk_channelio")));
}

void test_hundeschule_selects_channelio_for_configured_disk_after_boot_config(void)
{
    volatile uint32_t * slot1 = (volatile uint32_t *)&fake_virtio_region[0x200];
    slot1[0x000 / 4] = 0x74726976U;
    slot1[0x004 / 4] = 1;
    slot1[0x008 / 4] = 2;
    fake_config = "driver /sbin/services/hund-unterbrict once\n"
                  "virtio-block-driver /dev/virtio3 /sbin/drivers/virtio_blk_channelio\n";

    HundeschuleRequiredProcesses required_processes = HundeschuleRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(0, Hundeschule__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(5, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("2", spawn_calls[1].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio1", spawn_calls[1].arg1);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_STRING("1", spawn_calls[2].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", spawn_calls[2].arg1);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/socket_router", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[3].path);
    TEST_ASSERT_EQUAL_STRING("0", spawn_calls[3].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio3", spawn_calls[3].arg1);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/hundeschau", spawn_calls[4].path);
    TEST_ASSERT_EQUAL_UINT64(7, fake_wait_ready_count);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio3", fake_wait_ready_paths[4]);
}

void test_hundeschule_respawns_bootstrap_driver_with_same_args(void)
{
    fake_child_event_count = 1;
    fake_child_events[0] = (ProcessChildEvent){
        .pid = 3,
        .status = 0,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    TEST_ASSERT_EQUAL_INT(1, hundeschule_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(8, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/virtio_blk_channelio", spawn_calls[7].path);
    TEST_ASSERT_EQUAL_STRING("0", spawn_calls[7].arg0);
    TEST_ASSERT_EQUAL_STRING("/dev/virtio2", spawn_calls[7].arg1);
}

void test_hundeschule_does_not_respawn_once_driver(void)
{
    fake_child_event_count = 1;
    fake_child_events[0] = (ProcessChildEvent){
        .pid = 4,
        .status = 0,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    TEST_ASSERT_EQUAL_INT(1, hundeschule_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(7, spawn_call_count);
}

void test_hundeschule_lookup_handles_missing_pid_and_updates_stored_record(void)
{
    HundeschuleRequiredProcesses processes = HundeschuleRequiredProcesses__new();
    match_optional(Hundeschule__find_required_process(&processes, 42),
        some_test_fail(),
        none_do_nothing()
    );
    HundeschuleRequiredProcess first =
        HundeschuleRequiredProcess__new(11, true, &bstr("/first"), &empty_bstr(), &empty_bstr());
    HundeschuleRequiredProcess last =
        HundeschuleRequiredProcess__new(42, true, &bstr("/last"), &empty_bstr(), &empty_bstr());
    match_result(HundeschuleRequiredProcesses__push(&processes, first),
        success_do_nothing(),
        error_test_fail()
    );
    match_result(HundeschuleRequiredProcesses__push(&processes, last),
        success_do_nothing(),
        error_test_fail()
    );
    match_optional(Hundeschule__find_required_process(&processes, 99),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        Hundeschule__find_required_process(&processes, 42),
        some(process)
        {
            TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/last"));
            process->pid = 84;
        },
        none_test_fail()
    );
    match_optional(Hundeschule__find_required_process(&processes, 42),
        some_test_fail(),
        none_do_nothing()
    );
    match_optional(
        Hundeschule__find_required_process(&processes, 84),
        some(process)
        {
            TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/last"));
        },
        none_test_fail()
    );
    match_optional(
        Hundeschule__find_required_process(&processes, 11),
        some(process)
        {
            TEST_ASSERT_EQUAL_BSTRING(process->path, bstr("/first"));
        },
        none_test_fail()
    );
}

void test_hundeschule_next_token_handles_whitespace_and_exhaustion(void)
{
    BString remaining = bstr(" \tdriver \t /sbin/demo  once \r ");
    match_optional(
        Hundeschule__next_token(&remaining),
        some(token)
        {
            TEST_ASSERT_EQUAL_BSTRING(token, bstr("driver"));
        },
        none_test_fail()
    );
    match_optional(
        Hundeschule__next_token(&remaining),
        some(token)
        {
            TEST_ASSERT_EQUAL_BSTRING(token, bstr("/sbin/demo"));
        },
        none_test_fail()
    );
    match_optional(
        Hundeschule__next_token(&remaining),
        some(token)
        {
            TEST_ASSERT_EQUAL_BSTRING(token, bstr("once"));
        },
        none_test_fail()
    );
    match_optional(Hundeschule__next_token(&remaining),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_TRUE(BString__is_empty(&remaining));
    remaining = bstr(" \t\r ");
    match_optional(Hundeschule__next_token(&remaining),
        some_test_fail(),
        none_do_nothing()
    );
    TEST_ASSERT_TRUE(BString__is_empty(&remaining));
}

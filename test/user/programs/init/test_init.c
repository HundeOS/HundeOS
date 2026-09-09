#include <stdint.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/spawn.h"
#include "../../../../common_includes/syscalls/process.h"
#include "../../../../common_includes/syscalls/service.h"
#include "../../../../common_includes/syscalls/io.h"

typedef struct SpawnCall
{
    SpawnMode mode;
    uint8_t has_stdio;
    SpawnFileDescriptor stdin_fd;
    SpawnFileDescriptor stdout_fd;
    SpawnFileDescriptor stderr_fd;
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

static SpawnCall spawn_calls[10];
static uint64_t spawn_call_count;
static const char * failing_path;
static char printed_output[256];
static uint64_t printed_output_len;
static uint8_t fake_virtio_region[0x200ULL * 32ULL];
static ServiceMappedMemory fake_mapping;
static ProcessChildEvent fake_child_events[4];
static uint64_t fake_child_event_count;
static uint64_t fake_child_event_index;
static uint64_t fake_open_count;
static const char * fake_sysinfo;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;
static const char * fake_config;
static bool fake_config_opened;
static bool fake_config_closed;
static bool fake_expect_keyboard_input;

#define HUNDESCHAU_ENTRYPOINT hundeschau_main_under_test
#define IO__printf test_printf
#include "../../../../user/services/hundeschau/main.c"
#undef IO__printf

void setUp(void)
{
    spawn_call_count = 0;
    failing_path = NULL;
    printed_output_len = 0;
    for (uint64_t i = 0; i < 10; i++)
    {
        spawn_calls[i] = (SpawnCall){0};
    }
    for (uint64_t i = 0; i < sizeof(printed_output); i++)
    {
        printed_output[i] = '\0';
    }

    fake_mapping = (ServiceMappedMemory){
        .driver_address = (uint64_t)fake_virtio_region,
        .device_address = 0x0a000000ULL,
        .size = sizeof(fake_virtio_region),
    };

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
    fake_open_count = 0;
    fake_sysinfo = "framebuffer.present=0\n";
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    fake_config = NULL;
    fake_config_opened = false;
    fake_config_closed = false;
    fake_expect_keyboard_input = false;
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
    TEST_ASSERT_LESS_THAN_UINT64(10, spawn_call_count);
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
    TEST_ASSERT_LESS_THAN_UINT64(10, spawn_call_count);
    spawn_calls[spawn_call_count].mode = mode;
    spawn_calls[spawn_call_count].has_stdio = 1;
    spawn_calls[spawn_call_count].stdin_fd = stdin_fd;
    spawn_calls[spawn_call_count].stdout_fd = stdout_fd;
    spawn_calls[spawn_call_count].stderr_fd = stderr_fd;
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

SpawnResult spawn_as(SpawnMode mode, UserID requested_user_id, ConstBStringPtr path, OptionalPtr(BString) arg0,
                     OptionalPtr(BString) arg1, OptionalPtr(BString) arg2)
{
    TEST_FAIL();
    return SPAWN_FAILED;
}

IOHandle open(ConstBStringPtr path, Flags flags)
{
    if (strings_equal(BString__cstr(path), "/proc/sysinfo"))
    {
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
        fake_sysinfo_opened = true;
        return 1;
    }

    if (strings_equal(BString__cstr(path), "/boot/config/hundeschau.conf"))
    {
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
        if (fake_config == NULL)
        {
            return IO_INVALID_PATH;
        }
        fake_config_opened = true;
        return 2;
    }

    if (fake_open_count == 0)
    {
        TEST_ASSERT_EQUAL_STRING(fake_expect_keyboard_input ? ":keyboard:" : ":tty:", BString__cstr(path));
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
    }
    else if (fake_open_count == 1)
    {
        TEST_ASSERT_EQUAL_STRING(":console:", BString__cstr(path));
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE, flags);
    }
    else
    {
        TEST_ASSERT_EQUAL_STRING(":console:", BString__cstr(path));
        TEST_ASSERT_EQUAL_UINT64(IO_OPEN_WRITE, flags);
    }

    fake_open_count++;
    return (IOHandle)(fake_open_count + 6);
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    const char * source = NULL;
    if (handle == 1)
    {
        source = fake_sysinfo;
    }
    else if (handle == 2)
    {
        source = fake_config;
    }
    else
    {
        TEST_FAIL();
    }

    uint64_t index = 0;
    while (source[index] != '\0' && index < size)
    {
        buf[index] = source[index];
        index++;
    }
    return (ByteCount)index;
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
        error(code, message) { return None(DynString); }
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

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (void *)(memory->driver_address + offset);
}

int test_printf(ConstBStringPtr format, ...)
{
    const char * text = BString__cstr(format);
    uint64_t i = 0;
    while (text[i] != '\0' && printed_output_len + 1 < sizeof(printed_output))
    {
        printed_output[printed_output_len++] = text[i++];
    }
    printed_output[printed_output_len] = '\0';
    return (int)i;
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

void test_init_spawns_required_processes_in_background(void)
{
    HundeschauRequiredProcesses required_processes = HundeschauRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(0, Hundeschau__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(2, spawn_call_count);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[0].mode);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/humpen", spawn_calls[0].path);
    TEST_ASSERT_EQUAL(P_NOWAIT, spawn_calls[1].mode);
    TEST_ASSERT_EQUAL_UINT8(1, spawn_calls[1].has_stdio);
    TEST_ASSERT_EQUAL_UINT64(7, spawn_calls[1].stdin_fd);
    TEST_ASSERT_EQUAL_UINT64(8, spawn_calls[1].stdout_fd);
    TEST_ASSERT_EQUAL_UINT64(9, spawn_calls[1].stderr_fd);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_UINT64(3, fake_open_count);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
}

void test_init_spawns_framebuffer_keyboard_console(void)
{
    fake_expect_keyboard_input = true;
    fake_sysinfo = "framebuffer.present=1\n"
                   "framebuffer.physical_base=0x3e000000\n"
                   "framebuffer.physical_size=0x20000\n"
                   "framebuffer.width=512\n"
                   "framebuffer.height=64\n"
                   "framebuffer.pixels_per_scanline=512\n"
                   "framebuffer.pixel_format=0\n";

    HundeschauRequiredProcesses required_processes = HundeschauRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(0, Hundeschau__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(3, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/humpen", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/video_term", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_UINT8(1, spawn_calls[2].has_stdio);
    TEST_ASSERT_EQUAL_UINT64(7, spawn_calls[2].stdin_fd);
    TEST_ASSERT_EQUAL_UINT64(8, spawn_calls[2].stdout_fd);
    TEST_ASSERT_EQUAL_UINT64(9, spawn_calls[2].stderr_fd);
    TEST_ASSERT_EQUAL_UINT64(3, fake_open_count);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
}

void test_init_uses_boot_config_for_driver_list(void)
{
    fake_expect_keyboard_input = true;
    fake_sysinfo = "framebuffer.present=1\n"
                   "framebuffer.physical_base=0x3e000000\n"
                   "framebuffer.physical_size=0x20000\n"
                   "framebuffer.width=512\n"
                   "framebuffer.height=64\n"
                   "framebuffer.pixels_per_scanline=512\n"
                   "framebuffer.pixel_format=0\n";
    fake_config = "# hundeschau driver config\n"
                  "driver /sbin/services/humpen once\n"
                  "driver /sbin/drivers/video_term once framebuffer\n"
                  "driver /sbin/drivers/usb_keyboard respawn framebuffer\n";

    HundeschauRequiredProcesses required_processes = HundeschauRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(0, Hundeschau__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(4, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/humpen", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/video_term", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/drivers/usb_keyboard", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[3].path);
    TEST_ASSERT_EQUAL_UINT8(1, spawn_calls[3].has_stdio);
    TEST_ASSERT_TRUE(fake_config_opened);
    TEST_ASSERT_TRUE(fake_config_closed);
}

void test_init_returns_failure_when_required_driver_fails(void)
{
    failing_path = "/sbin/services/humpen";

    HundeschauRequiredProcesses required_processes = HundeschauRequiredProcesses__new();
    TEST_ASSERT_EQUAL_INT(1, Hundeschau__initialise_required_processes(&required_processes));

    TEST_ASSERT_EQUAL_UINT64(2, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/services/humpen", spawn_calls[0].path);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[1].path);
    TEST_ASSERT_EQUAL_UINT8(1, spawn_calls[1].has_stdio);
}

void test_init_respawns_required_process_when_child_event_arrives(void)
{
    fake_child_event_count = 1;
    fake_child_events[0] = (ProcessChildEvent){
        .pid = 2,
        .status = 0,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    TEST_ASSERT_EQUAL_INT(1, hundeschau_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(3, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[2].path);
}

void test_init_does_not_respawn_hundebus_when_child_event_arrives(void)
{
    fake_child_event_count = 1;
    fake_child_events[0] = (ProcessChildEvent){
        .pid = 3,
        .status = 0,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    TEST_ASSERT_EQUAL_INT(1, hundeschau_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(2, spawn_call_count);
}

void test_init_respawns_login_with_same_stdio(void)
{
    fake_child_event_count = 1;
    fake_child_events[0] = (ProcessChildEvent){
        .pid = 2,
        .status = 0,
        .type = PROCESS_CHILD_EVENT_EXITED,
    };

    TEST_ASSERT_EQUAL_INT(1, hundeschau_main_under_test());

    TEST_ASSERT_EQUAL_UINT64(3, spawn_call_count);
    TEST_ASSERT_EQUAL_STRING("/sbin/login", spawn_calls[2].path);
    TEST_ASSERT_EQUAL_UINT8(1, spawn_calls[2].has_stdio);
    TEST_ASSERT_EQUAL_UINT64(7, spawn_calls[2].stdin_fd);
    TEST_ASSERT_EQUAL_UINT64(8, spawn_calls[2].stdout_fd);
    TEST_ASSERT_EQUAL_UINT64(9, spawn_calls[2].stderr_fd);
}

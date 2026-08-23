#include <stdbool.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#include "unity.h"

#include "../../../../common_includes/syscalls/io.h"
#include "../../../../common_includes/syscalls/service.h"

#define FRAMEBUFFER_PHYS_BASE 0x3e000000ULL
#define FRAMEBUFFER_WIDTH 512ULL
#define FRAMEBUFFER_HEIGHT 64ULL
#define FRAMEBUFFER_STRIDE 512ULL
#define FRAMEBUFFER_SIZE (FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * sizeof(uint32_t))
#define FRAMEBUFFER_GUARD_WORDS 256ULL

struct ServiceMappedMemory
{
    void * driver_address;
    uint64_t device_address;
    uint64_t size;
};

static uint32_t fake_framebuffer[(FRAMEBUFFER_SIZE / sizeof(uint32_t)) + FRAMEBUFFER_GUARD_WORDS];
static char * fake_io_buffer;
static uint64_t fake_io_buffer_size;
static ServiceMappedMemory fake_framebuffer_mapping;
static uint32_t fake_pixel_format;
static uint64_t fake_red_mask;
static uint64_t fake_green_mask;
static uint64_t fake_blue_mask;
static uint64_t fake_reserved_mask;
static bool fake_became_service;
static bool fake_data_consumed;
static uint64_t fake_service_wait_count;
static bool fake_sysinfo_opened;
static bool fake_sysinfo_closed;

static char fake_sysinfo[1024];

static void VideoTermTest__set_input(char * input)
{
    fake_io_buffer = input;
    fake_io_buffer_size = 0;
    while (input[fake_io_buffer_size] != '\0')
    {
        fake_io_buffer_size++;
    }
}

static void build_fake_sysinfo(void)
{
    snprintf(
        fake_sysinfo,
        sizeof(fake_sysinfo),
    "framebuffer.present=1\n"
    "framebuffer.physical_base=0x3e000000\n"
    "framebuffer.physical_size=0x20000\n"
    "framebuffer.physical_size_human=128 KiB\n"
    "framebuffer.width=512\n"
    "framebuffer.height=64\n"
    "framebuffer.pixels_per_scanline=512\n"
    "framebuffer.pixel_format=%u\n"
    "framebuffer.red_mask=0x%llx\n"
    "framebuffer.green_mask=0x%llx\n"
    "framebuffer.blue_mask=0x%llx\n"
    "framebuffer.reserved_mask=0x%llx\n",
        (unsigned int) fake_pixel_format,
        (unsigned long long) fake_red_mask,
        (unsigned long long) fake_green_mask,
        (unsigned long long) fake_blue_mask,
        (unsigned long long) fake_reserved_mask);
}

static const uint8_t fake_font_data[] =
    "0020:00000000000000000000000000000000\n"
    "0061:0000000000003C42023E4242463A0000\n"
    "01F600:000003E00C181004200226324631400140014FF92AAA26B213E40C1803E00000\n"
    "0FFFD:000000007E42425A5A4242427E000000\n";

static uint8_t fake_emoji_atlas[16 * 1024 * 1024];

#define VIDEO_TERM_ENTRYPOINT video_term_main_under_test
#define VIDEO_TERM_FONT_START fake_font_data
#define VIDEO_TERM_FONT_END (fake_font_data + sizeof(fake_font_data) - 1)
#define VIDEO_TERM_EMOJI_ATLAS_START fake_emoji_atlas
#define VIDEO_TERM_EMOJI_ATLAS_END (fake_emoji_atlas + sizeof(fake_emoji_atlas))
#include "../../../../user/drivers/video_term/main.c"

static void VideoTermTest__seed_emoji_pixel(
    uint32_t codepoint,
    uint64_t x,
    uint64_t y,
    uint8_t red,
    uint8_t green,
    uint8_t blue)
{
    uint32_t codepoints[1] = { codepoint };
    match_optional(VideoTermEmojiEntryPtr, VideoTerm__find_emoji(codepoints, 1), some(emoji) {
            uint64_t emoji_pixel = emoji->atlas_offset + (((y * VIDEO_TERM_EMOJI_SIZE) + x) * 4);
            fake_emoji_atlas[emoji_pixel] = red;
            fake_emoji_atlas[emoji_pixel + 1] = green;
            fake_emoji_atlas[emoji_pixel + 2] = blue;
            fake_emoji_atlas[emoji_pixel + 3] = 0xff;
        }, none() { TEST_FAIL_MESSAGE("generated emoji index should include expected codepoint"); });
}

void setUp(void)
{
    fake_pixel_format = FRAMEBUFFER_CONFIG_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR;
    fake_red_mask = 0xff0000;
    fake_green_mask = 0xff00;
    fake_blue_mask = 0xff;
    fake_reserved_mask = 0xff000000;
    build_fake_sysinfo();

    fake_framebuffer_mapping.driver_address = fake_framebuffer;
    fake_framebuffer_mapping.device_address = FRAMEBUFFER_PHYS_BASE;
    fake_framebuffer_mapping.size = FRAMEBUFFER_SIZE;
    fake_became_service = false;
    fake_data_consumed = false;
    fake_service_wait_count = 0;
    fake_sysinfo_opened = false;
    fake_sysinfo_closed = false;
    VideoTermTest__set_input("a\xf0\x9f\x98\x80");

    for (uint64_t i = 0; i < FRAMEBUFFER_SIZE / sizeof(uint32_t); i++)
    {
        fake_framebuffer[i] = 0xffffffffU;
    }
    for (uint64_t i = FRAMEBUFFER_SIZE / sizeof(uint32_t);
         i < (FRAMEBUFFER_SIZE / sizeof(uint32_t)) + FRAMEBUFFER_GUARD_WORDS;
         i++)
    {
        fake_framebuffer[i] = 0xfeedfaceU;
    }

    for (uint64_t i = 0; i < sizeof(fake_emoji_atlas); i++)
    {
        fake_emoji_atlas[i] = 0;
    }

    VideoTermTest__seed_emoji_pixel(0x1f600, 12, 2, 0x12, 0x34, 0x56);
}

void tearDown(void)
{
}

SyslogResult kernel_syslogf(const char * format, ...)
{
    return SYSLOG_OK;
}

SyslogResult kernel_syslog_tagf(const char * tag, const char * format, ...)
{
    return SYSLOG_OK;
}

ServiceCode service_become_service()
{
    fake_became_service = true;
    return SERVICE_OK;
}

ServiceCode service_map_mmio(uint64_t physical_address, uint64_t size, ServiceMappedMemoryPtr * memory)
{
    TEST_ASSERT_EQUAL_UINT64(FRAMEBUFFER_PHYS_BASE, physical_address);
    TEST_ASSERT_EQUAL_UINT64(FRAMEBUFFER_SIZE, size);
    *memory = &fake_framebuffer_mapping;
    return SERVICE_OK;
}

IOHandle open(const char * path, Flags flags)
{
    TEST_ASSERT_EQUAL_STRING("/proc/sysinfo", path);
    TEST_ASSERT_EQUAL_UINT64(IO_OPEN_READ, flags);
    fake_sysinfo_opened = true;
    return 7;
}

ByteCount read(IOHandle handle, char * buf, size_t size)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    size_t index = 0;
    while (fake_sysinfo[index] != '\0' && index < size)
    {
        buf[index] = fake_sysinfo[index];
        index++;
    }
    return (ByteCount) index;
}

IOStatus close(IOHandle handle)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    fake_sysinfo_closed = true;
    return IO_OK;
}

Optional(DynString) IO__read_dyn_string(IOHandle handle, ByteCount max_bytes)
{
    TEST_ASSERT_EQUAL_INT64(7, handle);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    DynString sysinfo;
    match_result(DynStringCode, DynString__init(&sysinfo, 128), success(_) {}, error(code, message) { return None(DynString); })
    for (ByteCount index = 0; fake_sysinfo[index] != '\0' && index < max_bytes; index++)
    {
        match_result(DynStringCode, DynString__push_char(&sysinfo, fake_sysinfo[index]), success(_) {}, error(code, message) {
                DynString__dispose(&sysinfo);
                return None(DynString);
            })
    }
    return Some(DynString, sysinfo);
}

void * driver_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->driver_address;
}

uint64_t device_ptr(ServiceMappedMemoryPtr memory)
{
    return memory->device_address;
}

void * driver_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return (void *) ((uint64_t) memory->driver_address + offset);
}

uint64_t device_ptr_offset(ServiceMappedMemoryPtr memory, uint64_t offset)
{
    return memory->device_address + offset;
}

uint64_t mapped_size(ServiceMappedMemoryPtr memory)
{
    return memory->size;
}

IOHandle io_server_register(const char * path, IOServerRole role)
{
    TEST_ASSERT_EQUAL_STRING(":console:", path);
    TEST_ASSERT_EQUAL_INT(IO_SERVER_WRITE, role);
    return 43;
}

ServiceCode service_subscribe_irq(uint64_t irq)
{
    TEST_FAIL_MESSAGE("video_term driver should not subscribe to an irq");
    return SERVICE_OK;
}

ServiceCode service_wait(ServiceEventPtr event)
{
    fake_service_wait_count++;
    TEST_ASSERT_LESS_THAN_UINT64(3, fake_service_wait_count);
    if (fake_data_consumed)
    {
        return SERVICE_COMPLETE;
    }

    event->type = SERVICE_EVENT_IO;
    event->source = SERVICE_IO_WRITE_REQUEST;
    event->data = 0;
    return SERVICE_OK;
}

IOStatus io_server_get_buffer(IOHandle handle, IOServerBufferPtr buffer)
{
    TEST_ASSERT_EQUAL_INT64(43, handle);
    buffer->data = fake_io_buffer;
    buffer->capacity = fake_io_buffer_size;
    return IO_OK;
}

IOStatus io_server_data_available(IOHandle handle, uint64_t byte_count)
{
    TEST_FAIL_MESSAGE("video_term driver should not publish read data");
    return IO_OK;
}

IOStatus io_server_data_consumed(IOHandle handle, uint64_t byte_count)
{
    TEST_ASSERT_EQUAL_INT64(43, handle);
    TEST_ASSERT_EQUAL_UINT64(fake_io_buffer_size, byte_count);
    fake_data_consumed = true;
    return IO_OK;
}

IOStatus io_server_eof(IOHandle handle)
{
    TEST_FAIL_MESSAGE("video_term driver should not signal eof");
    return IO_OK;
}

static void VideoTermTest__run_and_assert(uint32_t expected_foreground_pixel, uint32_t expected_emoji_pixel)
{
    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);

    bool found_foreground_pixel = false;
    for (uint64_t row = 0; row < VIDEO_TERM_CELL_HEIGHT; row++)
    {
        for (uint64_t col = 0; col < VIDEO_TERM_COLUMN_WIDTH; col++)
        {
            if (fake_framebuffer[(row * FRAMEBUFFER_STRIDE) + col] == expected_foreground_pixel)
            {
                found_foreground_pixel = true;
            }
        }
    }
    TEST_ASSERT_TRUE_MESSAGE(found_foreground_pixel, "text cell should include a foreground pixel");

    TEST_ASSERT_EQUAL_UINT32(expected_emoji_pixel, fake_framebuffer[(1 * FRAMEBUFFER_STRIDE) + 14]);
    TEST_ASSERT_EQUAL_UINT32(0x00000000U, fake_framebuffer[(1 * FRAMEBUFFER_STRIDE) + 22]);
}

static bool VideoTermTest__cell_has_pixel(uint64_t cell_x, uint32_t color)
{
    uint64_t origin_x = cell_x * VIDEO_TERM_COLUMN_WIDTH;
    for (uint64_t row = 0; row < VIDEO_TERM_CELL_HEIGHT; row++)
    {
        for (uint64_t col = 0; col < VIDEO_TERM_COLUMN_WIDTH; col++)
        {
            if (fake_framebuffer[(row * FRAMEBUFFER_STRIDE) + origin_x + col] == color)
            {
                return true;
            }
        }
    }

    return false;
}

static void VideoTermTest__assert_cell_has_no_pixel(uint64_t cell_x, uint32_t color)
{
    TEST_ASSERT_FALSE(VideoTermTest__cell_has_pixel(cell_x, color));
}

static void VideoTermTest__assert_cell_filled(uint64_t cell_x, uint32_t color)
{
    uint64_t origin_x = cell_x * VIDEO_TERM_COLUMN_WIDTH;
    for (uint64_t row = 0; row < VIDEO_TERM_CELL_HEIGHT; row++)
    {
        for (uint64_t col = 0; col < VIDEO_TERM_COLUMN_WIDTH; col++)
        {
            TEST_ASSERT_EQUAL_UINT32(color, fake_framebuffer[(row * FRAMEBUFFER_STRIDE) + origin_x + col]);
        }
    }
}

static void VideoTermTest__set_shell_redraw_stress_input(void)
{
    static char redraw_input[12000];
    uint64_t cursor = 0;

    for (uint64_t redraw = 0; redraw < 60; redraw++)
    {
        const char * prefix = "\r\033[2K> ";
        for (uint64_t i = 0; prefix[i] != '\0'; i++)
        {
            redraw_input[cursor] = prefix[i];
            cursor++;
        }

        for (uint64_t i = 0; i <= redraw; i++)
        {
            redraw_input[cursor] = 'a';
            cursor++;
        }
    }

    redraw_input[cursor] = '\0';
    fake_io_buffer = redraw_input;
    fake_io_buffer_size = cursor;
}

void test_video_term_main_maps_framebuffer_draws_text_and_consumes_data(void)
{
    VideoTermTest__run_and_assert(0x00d8d8d8U, 0x00123456U);
}

void test_video_term_main_packs_color_with_bitmask_framebuffer(void)
{
    fake_pixel_format = FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BIT_MASK;
    fake_red_mask = 0xff;
    fake_green_mask = 0xff00;
    fake_blue_mask = 0xff0000;
    fake_reserved_mask = 0xff000000;
    build_fake_sysinfo();

    VideoTermTest__run_and_assert(0x00d8d8d8U, 0x00563412U);
}

void test_video_term_main_draws_hundeschau_banner_dog_emojis(void)
{
    VideoTermTest__set_input("\xf0\x9f\x90\xb6 HundeOS: hundeschau begins \xf0\x9f\x90\xbe\n");
    VideoTermTest__seed_emoji_pixel(0x1f436, 12, 2, 0x21, 0x43, 0x65);
    VideoTermTest__seed_emoji_pixel(0x1f43e, 12, 2, 0x87, 0x65, 0x43);

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_became_service);
    TEST_ASSERT_TRUE(fake_sysinfo_opened);
    TEST_ASSERT_TRUE(fake_sysinfo_closed);
    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_EQUAL_UINT64(2, fake_service_wait_count);
    TEST_ASSERT_EQUAL_UINT32(0x00214365U, fake_framebuffer[(1 * FRAMEBUFFER_STRIDE) + 6]);
    TEST_ASSERT_EQUAL_UINT32(0x00876543U, fake_framebuffer[(1 * FRAMEBUFFER_STRIDE) + (32 * VIDEO_TERM_COLUMN_WIDTH) + 6]);
}

void test_video_term_main_draws_plain_digits_with_text_font(void)
{
    VideoTermTest__set_input("1a");
    VideoTermTest__seed_emoji_pixel(0x31, 20, 1, 0xab, 0xcd, 0xef);

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(1, 0x00d8d8d8U));
    VideoTermTest__assert_cell_has_no_pixel(0, 0x00abcdefU);
    VideoTermTest__assert_cell_has_no_pixel(1, 0x00abcdefU);
}

void test_video_term_main_draws_block_cursor_after_written_text(void)
{
    VideoTermTest__set_input("a");

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
    VideoTermTest__assert_cell_filled(1, 0x00ffffffU);
}

void test_video_term_cursor_hide_restores_cell_pixels(void)
{
    FramebufferConfig config = {
        .physical_base = FRAMEBUFFER_PHYS_BASE,
        .physical_size = FRAMEBUFFER_SIZE,
        .width = FRAMEBUFFER_WIDTH,
        .height = FRAMEBUFFER_HEIGHT,
        .pixels_per_scanline = FRAMEBUFFER_STRIDE,
        .pixel_format = FRAMEBUFFER_CONFIG_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR,
        .red_mask = 0xff0000,
        .green_mask = 0xff00,
        .blue_mask = 0xff,
        .reserved_mask = 0xff000000,
    };
    VideoTerm terminal = VideoTerm__new(config, fake_framebuffer, FRAMEBUFFER_SIZE);

    VideoTerm__draw_codepoint(&terminal, 'a');
    VideoTerm__show_cursor(&terminal);
    VideoTermTest__assert_cell_filled(1, 0x00ffffffU);

    VideoTerm__hide_cursor(&terminal);
    VideoTermTest__assert_cell_filled(1, 0x00000000U);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
}

void test_video_term_main_handles_shell_clear_line_sequence(void)
{
    VideoTermTest__set_input("aaa\r\033[2K> a");

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
    VideoTermTest__assert_cell_has_no_pixel(1, 0x00d8d8d8U);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(2, 0x00d8d8d8U));
    VideoTermTest__assert_cell_has_no_pixel(3, 0x00d8d8d8U);
}

void test_video_term_main_handles_shell_cursor_left_sequence(void)
{
    VideoTermTest__set_input("aa\033[1Db");

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(1, 0x00d8d8d8U));
    VideoTermTest__assert_cell_has_no_pixel(2, 0x00d8d8d8U);
}

void test_video_term_main_bounds_writes_to_mapped_framebuffer_size(void)
{
    fake_io_buffer = "a\n"
        "a\n"
        "a\n"
        "a";
    fake_io_buffer_size = 7;
    fake_framebuffer_mapping.size = FRAMEBUFFER_WIDTH * FRAMEBUFFER_HEIGHT * sizeof(uint32_t);
    fake_sysinfo[0] = '\0';
    snprintf(
        fake_sysinfo,
        sizeof(fake_sysinfo),
        "framebuffer.present=1\n"
        "framebuffer.physical_base=0x3e000000\n"
        "framebuffer.physical_size=0x%llx\n"
        "framebuffer.physical_size_human=128 KiB\n"
        "framebuffer.width=%llu\n"
        "framebuffer.height=%llu\n"
        "framebuffer.pixels_per_scanline=%llu\n"
        "framebuffer.pixel_format=%u\n",
        (unsigned long long) fake_framebuffer_mapping.size,
        (unsigned long long) FRAMEBUFFER_WIDTH,
        (unsigned long long) FRAMEBUFFER_HEIGHT,
        (unsigned long long) (FRAMEBUFFER_WIDTH + 64ULL),
        (unsigned int) FRAMEBUFFER_CONFIG_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR);

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    for (uint64_t i = FRAMEBUFFER_SIZE / sizeof(uint32_t);
         i < (FRAMEBUFFER_SIZE / sizeof(uint32_t)) + FRAMEBUFFER_GUARD_WORDS;
         i++)
    {
        TEST_ASSERT_EQUAL_UINT32(0xfeedfaceU, fake_framebuffer[i]);
    }
}

void test_video_term_main_handles_repeated_shell_redraws(void)
{
    VideoTermTest__set_shell_redraw_stress_input();

    TEST_ASSERT_EQUAL_INT(0, video_term_main_under_test());

    TEST_ASSERT_TRUE(fake_data_consumed);
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(0, 0x00d8d8d8U));
    TEST_ASSERT_TRUE(VideoTermTest__cell_has_pixel(59, 0x00d8d8d8U));
    for (uint64_t i = FRAMEBUFFER_SIZE / sizeof(uint32_t);
         i < (FRAMEBUFFER_SIZE / sizeof(uint32_t)) + FRAMEBUFFER_GUARD_WORDS;
         i++)
    {
        TEST_ASSERT_EQUAL_UINT32(0xfeedfaceU, fake_framebuffer[i]);
    }
}

#include "unity.h"

#include "../../../kernel/panic.h"

static FramebufferInfo framebuffer;

void setUp(void)
{
    framebuffer = (FramebufferInfo) {
        .physical_base = 0x3e000000,
        .size = 0x200000,
        .width = 512,
        .height = 64,
        .pixels_per_scanline = 512,
        .pixel_format = FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR,
        .red_mask = 0xff0000,
        .green_mask = 0xff00,
        .blue_mask = 0xff,
        .reserved_mask = 0xff000000,
    };
}

void tearDown(void)
{
}

void test_panic_screen_uses_mapped_base_for_aligned_framebuffer(void)
{
    Address mapped_base = 0xffffa00000000000ULL;

    TEST_ASSERT_EQUAL_UINT64(mapped_base, PanicScreen__framebuffer_kernel_address(mapped_base, &framebuffer));
}

void test_panic_screen_adds_physical_offset_for_unaligned_framebuffer(void)
{
    Address mapped_base = 0xffffa00000000000ULL;
    framebuffer.physical_base = 0x3e001000ULL;

    TEST_ASSERT_EQUAL_UINT64(mapped_base + 0x1000ULL, PanicScreen__framebuffer_kernel_address(mapped_base, &framebuffer));
}

void test_panic_screen_mapped_rows_subtracts_physical_offset(void)
{
    framebuffer.physical_base = 0x3e001000ULL;

    TEST_ASSERT_EQUAL_UINT64(
        (PANIC_SCREEN_FRAMEBUFFER_MAP_SIZE - 0x1000ULL) / (512ULL * sizeof(uint32_t)),
        PanicScreen__mapped_rows(&framebuffer, PANIC_SCREEN_FRAMEBUFFER_MAP_SIZE)
    );
}

void test_panic_screen_mapped_rows_is_zero_without_scanline(void)
{
    framebuffer.pixels_per_scanline = 0;

    TEST_ASSERT_EQUAL_UINT64(0, PanicScreen__mapped_rows(&framebuffer, PANIC_SCREEN_FRAMEBUFFER_MAP_SIZE));
}

void test_panic_screen_font_can_render_panic_labels(void)
{
    TEST_ASSERT_TRUE(PanicScreen__can_render_text("kernel panik"));
    TEST_ASSERT_TRUE(PanicScreen__can_render_text("Hund tot"));
}

void test_panic_screen_font_can_render_fault_cause(void)
{
    TEST_ASSERT_TRUE(PanicScreen__can_render_text("Data abort from current EL"));
}

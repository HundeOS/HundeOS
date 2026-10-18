#include "unity.h"

#include "../../../kernel/artwork.h"
#include "../../../kernel/panic.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/kernel_init.h"
#include "../stubs/process_context_layout.h"

void ArtworkTest__reset(void);
const char * ArtworkTest__output(void);
extern uint64_t __framebuffer_start[];

static FramebufferInfo framebuffer;

void setUp(void)
{
    ArtworkTest__reset();
    framebuffer = (FramebufferInfo){
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

void test_system_artwork_uses_mapped_base_for_aligned_framebuffer(void)
{
    Address mapped_base = 0xffffa00000000000ULL;

    TEST_ASSERT_EQUAL_UINT64(mapped_base, SystemArtwork__framebuffer_kernel_address(mapped_base, &framebuffer));
}

void test_system_artwork_adds_physical_offset_for_unaligned_framebuffer(void)
{
    Address mapped_base = 0xffffa00000000000ULL;
    framebuffer.physical_base = 0x3e001000ULL;

    TEST_ASSERT_EQUAL_UINT64(mapped_base + 0x1000ULL,
                             SystemArtwork__framebuffer_kernel_address(mapped_base, &framebuffer));
}

void test_system_artwork_mapped_rows_subtracts_physical_offset(void)
{
    framebuffer.physical_base = 0x3e001000ULL;

    TEST_ASSERT_EQUAL_UINT64(framebuffer.size / (512ULL * sizeof(uint32_t)),
                             SystemArtwork__mapped_rows(&framebuffer, SYSTEM_ARTWORK_FRAMEBUFFER_MAP_SIZE));
}

void test_system_artwork_mapped_rows_clips_to_mapping_size(void)
{
    framebuffer.physical_base += 0x1000;
    TEST_ASSERT_EQUAL_UINT64(2, SystemArtwork__mapped_rows(&framebuffer, 0x2000));
}

void test_system_artwork_launch_and_panic_use_ascii_without_framebuffer(void)
{
    SystemInfo info = SystemInfo__new();
    SystemArtwork__show_launch(&info);
    SystemArtwork__show_panic(&info, "failure");
    TEST_ASSERT_EQUAL_STRING("logopanic", ArtworkTest__output());
}

void test_system_artwork_unsupported_framebuffer_uses_ascii(void)
{
    SystemInfo info = SystemInfo__new();
    framebuffer.pixel_format = FRAMEBUFFER_PIXEL_FORMAT_BLT_ONLY;
    info.framebuffer = Some(FramebufferInfo, framebuffer);
    SystemArtwork__show_launch(&info);
    TEST_ASSERT_EQUAL_STRING("logo", ArtworkTest__output());
}

void test_system_artwork_framebuffer_selects_distinct_images_and_clips_rows(void)
{
    SystemInfo info = SystemInfo__new();
    framebuffer.width = 1;
    framebuffer.height = 2;
    framebuffer.pixels_per_scanline = 1;
    framebuffer.size = sizeof(uint32_t);
    info.framebuffer = Some(FramebufferInfo, framebuffer);
    SystemArtwork__show_launch(&info);
    TEST_ASSERT_EQUAL_HEX64(0xff, __framebuffer_start[0]);
    SystemArtwork__show_panic(&info, "failure");
    TEST_ASSERT_EQUAL_HEX64(0xff00, __framebuffer_start[0]);
    TEST_ASSERT_EQUAL_STRING("", ArtworkTest__output());
}

void test_kernel_panic_without_system_info_keeps_ascii_and_diagnostics(void)
{
    Kernel__panic("failure", &((ProcessContext){0}));
    TEST_ASSERT_EQUAL_STRING("panicKernel panik: failure\nSystem halted\n", ArtworkTest__output());
}

void test_system_artwork_mapped_rows_is_zero_without_scanline(void)
{
    framebuffer.pixels_per_scanline = 0;

    TEST_ASSERT_EQUAL_UINT64(0, SystemArtwork__mapped_rows(&framebuffer, SYSTEM_ARTWORK_FRAMEBUFFER_MAP_SIZE));
}

void test_system_artwork_scales_and_centres_rgb_image(void)
{
    uint8_t image_pixels[] = {0xff, 0x00, 0x00, 0x00, 0xff, 0x00};
    SystemArtworkImage image = SystemArtworkImage__new(image_pixels, 2, 1);
    uint32_t pixels[16] = {0};
    framebuffer.width = 4;
    framebuffer.height = 4;
    framebuffer.pixels_per_scanline = 4;

    SystemArtwork__draw_image(pixels, &framebuffer, &image);

    TEST_ASSERT_EQUAL_HEX32(0, pixels[0]);
    TEST_ASSERT_EQUAL_HEX32(0x000000ff, pixels[4]);
    TEST_ASSERT_EQUAL_HEX32(0x0000ff00, pixels[6]);
    TEST_ASSERT_EQUAL_HEX32(0x000000ff, pixels[8]);
    TEST_ASSERT_EQUAL_HEX32(0, pixels[12]);
}

void test_system_artwork_packs_bgr_framebuffer_pixels(void)
{
    uint8_t image_pixels[] = {0xff, 0x00, 0x00};
    SystemArtworkImage image = SystemArtworkImage__new(image_pixels, 1, 1);
    uint32_t pixels[1] = {0};
    framebuffer.width = 1;
    framebuffer.height = 1;
    framebuffer.pixels_per_scanline = 1;
    framebuffer.pixel_format = FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR;

    SystemArtwork__draw_image(pixels, &framebuffer, &image);

    TEST_ASSERT_EQUAL_HEX32(0x00ff0000, pixels[0]);
}

void test_system_artwork_packs_bitmask_framebuffer_pixels_and_preserves_stride_padding(void)
{
    uint8_t image_pixels[] = {0xff, 0x80, 0x00};
    SystemArtworkImage image = SystemArtworkImage__new(image_pixels, 1, 1);
    uint32_t pixels[] = {0, 0xdeadbeef};
    framebuffer.width = 1;
    framebuffer.height = 1;
    framebuffer.pixels_per_scanline = 2;
    framebuffer.pixel_format = FRAMEBUFFER_PIXEL_FORMAT_BIT_MASK;
    framebuffer.red_mask = 0x3ff00000;
    framebuffer.green_mask = 0x000ffc00;
    framebuffer.blue_mask = 0x000003ff;

    SystemArtwork__draw_image(pixels, &framebuffer, &image);

    TEST_ASSERT_EQUAL_HEX32(0x3ff80800, pixels[0]);
    TEST_ASSERT_EQUAL_HEX32(0xdeadbeef, pixels[1]);
}

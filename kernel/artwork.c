#include "artwork.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "arch/basic_io.h"

#define SYSTEM_ARTWORK_WIDTH 768U
#define SYSTEM_ARTWORK_HEIGHT 512U
#define SYSTEM_ARTWORK_BLACK 0x00000000U
#define SYSTEM_ARTWORK_TEXT 0x00f8efe0U
#define SYSTEM_ARTWORK_TEXT_BG 0x00a00000U
#define SYSTEM_ARTWORK_TEXT_SCALE 3U
#define SYSTEM_ARTWORK_FRAMEBUFFER_BLOCK_SIZE (2ULL * 1024ULL * 1024ULL)

extern const uint64_t __framebuffer_start[] __attribute__((weak));
extern const char __hunde_logo_ascii[];
extern const char __hunde_panic_ascii[];
extern const uint8_t __hunde_logo_rgb_start[];
extern const uint8_t __hunde_panic_rgb_start[];

typedef struct SystemArtworkGlyph
{
    char character;
    uint8_t rows[7];
} SystemArtworkGlyph;

typedef const SystemArtworkGlyph * ConstSystemArtworkGlyphPtr;

static const SystemArtworkGlyph system_artwork_font[] = {
    {' ', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00}}, {'-', {0x00, 0x00, 0x00, 0x1f, 0x00, 0x00, 0x00}},
    {':', {0x00, 0x04, 0x04, 0x00, 0x04, 0x04, 0x00}}, {'/', {0x01, 0x01, 0x02, 0x04, 0x08, 0x10, 0x10}},
    {'_', {0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x1f}}, {'0', {0x0e, 0x11, 0x13, 0x15, 0x19, 0x11, 0x0e}},
    {'1', {0x04, 0x0c, 0x04, 0x04, 0x04, 0x04, 0x0e}}, {'2', {0x0e, 0x11, 0x01, 0x02, 0x04, 0x08, 0x1f}},
    {'3', {0x1e, 0x01, 0x01, 0x0e, 0x01, 0x01, 0x1e}}, {'4', {0x02, 0x06, 0x0a, 0x12, 0x1f, 0x02, 0x02}},
    {'5', {0x1f, 0x10, 0x10, 0x1e, 0x01, 0x01, 0x1e}}, {'6', {0x0e, 0x10, 0x10, 0x1e, 0x11, 0x11, 0x0e}},
    {'7', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x08, 0x08}}, {'8', {0x0e, 0x11, 0x11, 0x0e, 0x11, 0x11, 0x0e}},
    {'9', {0x0e, 0x11, 0x11, 0x0f, 0x01, 0x01, 0x0e}}, {'A', {0x0e, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}},
    {'B', {0x1e, 0x11, 0x11, 0x1e, 0x11, 0x11, 0x1e}}, {'C', {0x0e, 0x11, 0x10, 0x10, 0x10, 0x11, 0x0e}},
    {'D', {0x1e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x1e}}, {'E', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x1f}},
    {'F', {0x1f, 0x10, 0x10, 0x1e, 0x10, 0x10, 0x10}}, {'G', {0x0e, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0e}},
    {'H', {0x11, 0x11, 0x11, 0x1f, 0x11, 0x11, 0x11}}, {'I', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x1f}},
    {'J', {0x01, 0x01, 0x01, 0x01, 0x11, 0x11, 0x0e}}, {'K', {0x11, 0x12, 0x14, 0x18, 0x14, 0x12, 0x11}},
    {'L', {0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1f}}, {'M', {0x11, 0x1b, 0x15, 0x15, 0x11, 0x11, 0x11}},
    {'N', {0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11}}, {'O', {0x0e, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'P', {0x1e, 0x11, 0x11, 0x1e, 0x10, 0x10, 0x10}}, {'Q', {0x0e, 0x11, 0x11, 0x11, 0x15, 0x12, 0x0d}},
    {'R', {0x1e, 0x11, 0x11, 0x1e, 0x14, 0x12, 0x11}}, {'S', {0x0f, 0x10, 0x10, 0x0e, 0x01, 0x01, 0x1e}},
    {'T', {0x1f, 0x04, 0x04, 0x04, 0x04, 0x04, 0x04}}, {'U', {0x11, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0e}},
    {'V', {0x11, 0x11, 0x11, 0x11, 0x11, 0x0a, 0x04}}, {'W', {0x11, 0x11, 0x11, 0x15, 0x15, 0x15, 0x0a}},
    {'X', {0x11, 0x11, 0x0a, 0x04, 0x0a, 0x11, 0x11}}, {'Y', {0x11, 0x11, 0x0a, 0x04, 0x04, 0x04, 0x04}},
    {'Z', {0x1f, 0x01, 0x02, 0x04, 0x08, 0x10, 0x1f}},
};

static uint32_t SystemArtwork__pack_channel(uint32_t value, uint32_t mask)
{
    if (mask == 0)
    {
        return 0;
    }
    uint32_t shift = 0;
    while (((mask >> shift) & 1U) == 0)
    {
        shift++;
    }
    uint64_t maximum = mask >> shift;
    return (uint32_t)(((value * maximum + 127U) / 255U) << shift) & mask;
}

static uint32_t SystemArtwork__pack_color(FramebufferInfoPtr framebuffer, uint32_t color)
{
    uint32_t red = (color >> 16U) & 0xffU;
    uint32_t green = (color >> 8U) & 0xffU;
    uint32_t blue = color & 0xffU;
    switch (framebuffer->pixel_format)
    {
    case FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR:
        return (red << 16U) | (green << 8U) | blue;
    case FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR:
        return (blue << 16U) | (green << 8U) | red;
    case FRAMEBUFFER_PIXEL_FORMAT_BIT_MASK:
        return SystemArtwork__pack_channel(red, framebuffer->red_mask) |
               SystemArtwork__pack_channel(green, framebuffer->green_mask) |
               SystemArtwork__pack_channel(blue, framebuffer->blue_mask);
    default:
        return color;
    }
}

Address SystemArtwork__framebuffer_kernel_address(Address mapped_base, FramebufferInfoPtr framebuffer)
{
    uint64_t physical_offset = framebuffer->physical_base & (SYSTEM_ARTWORK_FRAMEBUFFER_BLOCK_SIZE - 1ULL);
    return mapped_base + physical_offset;
}

uint64_t SystemArtwork__mapped_rows(FramebufferInfoPtr framebuffer, uint64_t mapped_bytes)
{
    uint64_t physical_offset = framebuffer->physical_base & (SYSTEM_ARTWORK_FRAMEBUFFER_BLOCK_SIZE - 1ULL);
    if (mapped_bytes <= physical_offset || framebuffer->pixels_per_scanline == 0)
    {
        return 0;
    }
    uint64_t available_bytes = mapped_bytes - physical_offset;
    if (available_bytes > framebuffer->size)
    {
        available_bytes = framebuffer->size;
    }
    return available_bytes / ((uint64_t)framebuffer->pixels_per_scanline * sizeof(uint32_t));
}

static void SystemArtwork__put_pixel(SystemArtworkFramebufferPtr pixels, FramebufferInfoPtr framebuffer, uint32_t x,
                                     uint32_t y, uint32_t color)
{
    if (x < framebuffer->width && y < framebuffer->height)
    {
        pixels[(uint64_t)y * framebuffer->pixels_per_scanline + x] = SystemArtwork__pack_color(framebuffer, color);
    }
}

static void SystemArtwork__fill(SystemArtworkFramebufferPtr pixels, FramebufferInfoPtr framebuffer, uint32_t color)
{
    for (uint32_t y = 0; y < framebuffer->height; y++)
    {
        for (uint32_t x = 0; x < framebuffer->width; x++)
        {
            SystemArtwork__put_pixel(pixels, framebuffer, x, y, color);
        }
    }
}

void SystemArtwork__draw_image(SystemArtworkFramebufferPtr pixels, FramebufferInfoPtr framebuffer,
                               ConstSystemArtworkImagePtr image)
{
    if (framebuffer->width > framebuffer->pixels_per_scanline)
    {
        return;
    }
    SystemArtwork__fill(pixels, framebuffer, SYSTEM_ARTWORK_BLACK);
    if (image->width == 0 || image->height == 0 || framebuffer->width == 0 || framebuffer->height == 0)
    {
        return;
    }
    uint32_t draw_width = framebuffer->width;
    uint32_t draw_height = (uint32_t)(((uint64_t)image->height * draw_width) / image->width);
    if (draw_height > framebuffer->height)
    {
        draw_height = framebuffer->height;
        draw_width = (uint32_t)(((uint64_t)image->width * draw_height) / image->height);
    }
    if (draw_width == 0)
    {
        draw_width = 1;
    }
    if (draw_height == 0)
    {
        draw_height = 1;
    }
    uint32_t offset_x = (framebuffer->width - draw_width) / 2U;
    uint32_t offset_y = (framebuffer->height - draw_height) / 2U;
    for (uint32_t y = 0; y < draw_height; y++)
    {
        uint32_t source_y = (uint32_t)(((uint64_t)y * image->height) / draw_height);
        for (uint32_t x = 0; x < draw_width; x++)
        {
            uint32_t source_x = (uint32_t)(((uint64_t)x * image->width) / draw_width);
            uint64_t source = ((uint64_t)source_y * image->width + source_x) * 3U;
            uint32_t color = ((uint32_t)image->pixels[source] << 16U) | ((uint32_t)image->pixels[source + 1U] << 8U) |
                             image->pixels[source + 2U];
            SystemArtwork__put_pixel(pixels, framebuffer, offset_x + x, offset_y + y, color);
        }
    }
}

static char SystemArtwork__font_character(char character)
{
    return character >= 'a' && character <= 'z' ? (char)(character - ('a' - 'A')) : character;
}

static ConstSystemArtworkGlyphPtr SystemArtwork__glyph(char character)
{
    char rendered = SystemArtwork__font_character(character);
    for (size_t index = 0; index < sizeof(system_artwork_font) / sizeof(system_artwork_font[0]); index++)
    {
        if (system_artwork_font[index].character == rendered)
        {
            return &system_artwork_font[index];
        }
    }
    return &system_artwork_font[0];
}

static void SystemArtwork__draw_message(SystemArtworkFramebufferPtr pixels, FramebufferInfoPtr framebuffer,
                                        const char * message)
{
    uint32_t cursor_x = 24U;
    for (size_t index = 0; message[index] != '\0' && cursor_x + 18U < framebuffer->width; index++)
    {
        ConstSystemArtworkGlyphPtr glyph = SystemArtwork__glyph(message[index]);
        for (uint32_t row = 0; row < 7; row++)
        {
            for (uint32_t column = 0; column < 5; column++)
            {
                if ((glyph->rows[row] & (1U << (4U - column))) != 0)
                {
                    for (uint32_t y = 0; y < SYSTEM_ARTWORK_TEXT_SCALE; y++)
                    {
                        for (uint32_t x = 0; x < SYSTEM_ARTWORK_TEXT_SCALE; x++)
                        {
                            SystemArtwork__put_pixel(pixels, framebuffer,
                                                     cursor_x + column * SYSTEM_ARTWORK_TEXT_SCALE + x,
                                                     24U + row * SYSTEM_ARTWORK_TEXT_SCALE + y, SYSTEM_ARTWORK_TEXT);
                        }
                    }
                }
            }
        }
        cursor_x += 6U * SYSTEM_ARTWORK_TEXT_SCALE;
    }
}

static bool SystemArtwork__can_draw(FramebufferInfoPtr framebuffer)
{
    return (uintptr_t)__framebuffer_start != 0 && framebuffer->width != 0 && framebuffer->height != 0 &&
           framebuffer->width <= framebuffer->pixels_per_scanline &&
           (framebuffer->pixel_format == FRAMEBUFFER_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR ||
            framebuffer->pixel_format == FRAMEBUFFER_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR ||
            framebuffer->pixel_format == FRAMEBUFFER_PIXEL_FORMAT_BIT_MASK) &&
           SystemArtwork__mapped_rows(framebuffer, SYSTEM_ARTWORK_FRAMEBUFFER_MAP_SIZE) != 0;
}

static void SystemArtwork__draw_mapped(FramebufferInfoPtr framebuffer, ConstSystemArtworkImagePtr image,
                                       const char * message)
{
    FramebufferInfo render_framebuffer = *framebuffer;
    uint64_t mapped_rows = SystemArtwork__mapped_rows(framebuffer, SYSTEM_ARTWORK_FRAMEBUFFER_MAP_SIZE);
    if (render_framebuffer.height > mapped_rows)
    {
        render_framebuffer.height = (uint32_t)mapped_rows;
    }
    SystemArtworkFramebufferPtr pixels = (SystemArtworkFramebufferPtr)SystemArtwork__framebuffer_kernel_address(
        (Address)&__framebuffer_start[0], framebuffer);
    SystemArtwork__draw_image(pixels, &render_framebuffer, image);
    if (message[0] != '\0')
    {
        for (uint32_t y = 12U; y < 57U && y < render_framebuffer.height; y++)
        {
            for (uint32_t x = 12U; x + 12U < render_framebuffer.width; x++)
            {
                SystemArtwork__put_pixel(pixels, &render_framebuffer, x, y, SYSTEM_ARTWORK_TEXT_BG);
            }
        }
        SystemArtwork__draw_message(pixels, &render_framebuffer, message);
    }
}

static bool SystemArtwork__show_framebuffer(SystemInfoPtr system_info, ConstSystemArtworkImagePtr image,
                                            const char * message)
{
    match_optional(
        system_info->framebuffer,
        some(framebuffer)
        {
            if (SystemArtwork__can_draw(&framebuffer))
            {
                SystemArtwork__draw_mapped(&framebuffer, image, message);
                return true;
            }
        },
        none_do_nothing()
    );
    return false;
}

void SystemArtwork__show_launch(SystemInfoPtr system_info)
{
    SystemArtworkImage image =
        SystemArtworkImage__new(__hunde_logo_rgb_start, SYSTEM_ARTWORK_WIDTH, SYSTEM_ARTWORK_HEIGHT);
    if (!SystemArtwork__show_framebuffer(system_info, &image, ""))
    {
        BasicIO__puts(__hunde_logo_ascii);
    }
}

void SystemArtwork__show_panic(SystemInfoPtr system_info, const char * message)
{
    SystemArtworkImage image =
        SystemArtworkImage__new(__hunde_panic_rgb_start, SYSTEM_ARTWORK_WIDTH, SYSTEM_ARTWORK_HEIGHT);
    if (!SystemArtwork__show_framebuffer(system_info, &image, message))
    {
        SystemArtwork__show_panic_ascii();
    }
}

void SystemArtwork__show_panic_ascii(void)
{
    BasicIO__puts(__hunde_panic_ascii);
}

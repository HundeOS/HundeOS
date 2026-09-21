#include "panic.h"
#include "print.h"
#include "arch/basic_io.h"
#include "arch/cpu.h"
#include "kernel_core.h"
#include "kernel_init.h"

#define PANIC_SCREEN_BG 0x00101010U
#define PANIC_SCREEN_RED 0x00c02020U
#define PANIC_SCREEN_FG 0x00e8e8e8U
#define PANIC_SCREEN_DOG 0x00b58a5aU
#define PANIC_SCREEN_DARK 0x00302018U
#define PANIC_SCREEN_SCALE 4U
#define PANIC_SCREEN_FRAMEBUFFER_BLOCK_SIZE (2ULL * 1024ULL * 1024ULL)

extern const uint64_t __framebuffer_start[] __attribute__((weak));

typedef struct PanicBitmapGlyph
{
    char ch;
    uint8_t rows[7];
} PanicBitmapGlyph;

typedef PanicBitmapGlyph * PanicBitmapGlyphPtr;
typedef uint32_t * PanicScreenPixelsPtr;

static char PanicScreen__font_char(char ch)
{
    if (ch >= 'a' && ch <= 'z')
    {
        return (char)(ch - ('a' - 'A'));
    }

    return ch;
}

static const char * panic_dead_dog_ascii = "\n"
                                           "        /\\_/\\\\\n"
                                           "   ____/ x x \\\n"
                                           "  / __    ^  /\n"
                                           " /_/  \\_____/   Hund tot\n"
                                           "      /     \\\n"
                                           "     /_/   \\_\\\n"
                                           "\n";

static const PanicBitmapGlyph panic_font[] = {
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

static uint32_t PanicScreen__pack_color(FramebufferInfoPtr framebuffer, uint32_t color)
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
    default:
        return color;
    }
}

Address PanicScreen__framebuffer_kernel_address(Address mapped_base, FramebufferInfoPtr framebuffer)
{
    uint64_t physical_offset = framebuffer->physical_base & (PANIC_SCREEN_FRAMEBUFFER_BLOCK_SIZE - 1ULL);

    return mapped_base + physical_offset;
}

uint64_t PanicScreen__mapped_rows(FramebufferInfoPtr framebuffer, uint64_t mapped_bytes)
{
    uint64_t physical_offset = framebuffer->physical_base & (PANIC_SCREEN_FRAMEBUFFER_BLOCK_SIZE - 1ULL);
    if (mapped_bytes <= physical_offset || framebuffer->pixels_per_scanline == 0)
    {
        return 0;
    }

    uint64_t row_bytes = framebuffer->pixels_per_scanline * sizeof(uint32_t);
    if (row_bytes == 0)
    {
        return 0;
    }

    return (mapped_bytes - physical_offset) / row_bytes;
}

static void PanicScreen__put_pixel(PanicScreenPixelsPtr pixels, FramebufferInfoPtr framebuffer, uint32_t x, uint32_t y,
                                   uint32_t color)
{
    if (x >= framebuffer->width || y >= framebuffer->height)
    {
        return;
    }

    pixels[((uint64_t)y * framebuffer->pixels_per_scanline) + x] = PanicScreen__pack_color(framebuffer, color);
}

static void PanicScreen__fill_rect(PanicScreenPixelsPtr pixels, FramebufferInfoPtr framebuffer, int32_t x, int32_t y,
                                   uint32_t width, uint32_t height, uint32_t color)
{
    for (uint32_t row = 0; row < height; row++)
    {
        for (uint32_t col = 0; col < width; col++)
        {
            int64_t target_x = (int64_t)x + col;
            int64_t target_y = (int64_t)y + row;
            if (target_x >= 0 && target_y >= 0)
            {
                PanicScreen__put_pixel(pixels, framebuffer, (uint32_t)target_x, (uint32_t)target_y, color);
            }
        }
    }
}

static PanicBitmapGlyphPtr PanicScreen__glyph_for(char ch)
{
    char render_ch = PanicScreen__font_char(ch);
    size_t count = sizeof(panic_font) / sizeof(panic_font[0]);
    for (size_t index = 0; index < count; index++)
    {
        if (panic_font[index].ch == render_ch)
        {
            return (PanicBitmapGlyphPtr)&panic_font[index];
        }
    }

    return (PanicBitmapGlyphPtr)&panic_font[0];
}

bool PanicScreen__can_render_text(const char * text)
{
    for (size_t text_index = 0; text[text_index] != '\0'; text_index++)
    {
        char render_ch = PanicScreen__font_char(text[text_index]);
        bool found = false;
        size_t count = sizeof(panic_font) / sizeof(panic_font[0]);
        for (size_t glyph_index = 0; glyph_index < count; glyph_index++)
        {
            if (panic_font[glyph_index].ch == render_ch)
            {
                found = true;
                break;
            }
        }

        if (!found)
        {
            return false;
        }
    }

    return true;
}

static void PanicScreen__draw_text(PanicScreenPixelsPtr pixels, FramebufferInfoPtr framebuffer, int32_t x, int32_t y,
                                   const char * text, uint32_t color)
{
    int32_t cursor_x = x;
    for (size_t index = 0; text[index] != '\0'; index++)
    {
        if (text[index] == '\n')
        {
            return;
        }

        PanicBitmapGlyphPtr glyph = PanicScreen__glyph_for(text[index]);
        for (uint32_t row = 0; row < 7; row++)
        {
            for (uint32_t col = 0; col < 5; col++)
            {
                if ((glyph->rows[row] & (1U << (4U - col))) != 0)
                {
                    PanicScreen__fill_rect(pixels, framebuffer, cursor_x + (col * PANIC_SCREEN_SCALE),
                                           y + (row * PANIC_SCREEN_SCALE), PANIC_SCREEN_SCALE, PANIC_SCREEN_SCALE,
                                           color);
                }
            }
        }
        cursor_x += 6U * PANIC_SCREEN_SCALE;
    }
}

static void PanicScreen__draw_x(PanicScreenPixelsPtr pixels, FramebufferInfoPtr framebuffer, int32_t x, int32_t y,
                                uint32_t size, uint32_t color)
{
    for (uint32_t index = 0; index < size; index++)
    {
        PanicScreen__fill_rect(pixels, framebuffer, x + (int32_t)index, y + (int32_t)index, 3U, 3U, color);
        PanicScreen__fill_rect(pixels, framebuffer, x + (int32_t)(size - index), y + (int32_t)index, 3U, 3U, color);
    }
}

static void PanicScreen__draw_dead_dog(PanicScreenPixelsPtr pixels, FramebufferInfoPtr framebuffer)
{
    int32_t center_x = (int32_t)(framebuffer->width / 2U);
    int32_t center_y = (int32_t)(framebuffer->height / 2U);
    uint32_t dog = PANIC_SCREEN_DOG;
    uint32_t dark = PANIC_SCREEN_DARK;

    PanicScreen__fill_rect(pixels, framebuffer, center_x - 128, center_y + 18, 202U, 42U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x + 34, center_y - 10, 82U, 58U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x + 42, center_y - 34, 18U, 26U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x + 90, center_y - 34, 18U, 26U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x - 154, center_y + 26, 34U, 12U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x - 104, center_y + 60, 76U, 14U, dog);
    PanicScreen__fill_rect(pixels, framebuffer, center_x + 4, center_y + 60, 76U, 14U, dog);
    PanicScreen__draw_x(pixels, framebuffer, center_x + 52, center_y + 4, 14U, dark);
    PanicScreen__draw_x(pixels, framebuffer, center_x + 84, center_y + 4, 14U, dark);
    PanicScreen__fill_rect(pixels, framebuffer, center_x + 66, center_y + 28, 24U, 8U, dark);
    PanicScreen__fill_rect(pixels, framebuffer, center_x - 166, center_y + 82, 320U, 8U, PANIC_SCREEN_RED);
}

static void PanicScreen__render(FramebufferInfoPtr framebuffer, const char * message)
{
    if ((uintptr_t)__framebuffer_start == 0 || framebuffer->width == 0 || framebuffer->height == 0 ||
        framebuffer->pixels_per_scanline == 0)
    {
        return;
    }

    uint64_t mapped_rows = PanicScreen__mapped_rows(framebuffer, PANIC_SCREEN_FRAMEBUFFER_MAP_SIZE);
    if (mapped_rows == 0)
    {
        return;
    }

    FramebufferInfo render_framebuffer = *framebuffer;
    if (render_framebuffer.height > mapped_rows)
    {
        render_framebuffer.height = (uint32_t)mapped_rows;
    }

    PanicScreenPixelsPtr pixels =
        (PanicScreenPixelsPtr)PanicScreen__framebuffer_kernel_address((Address)&__framebuffer_start[0], framebuffer);
    PanicScreen__fill_rect(pixels, &render_framebuffer, 0, 0, render_framebuffer.width, render_framebuffer.height,
                           PANIC_SCREEN_BG);
    PanicScreen__fill_rect(pixels, &render_framebuffer, 0, 0, render_framebuffer.width, 10U, PANIC_SCREEN_RED);
    PanicScreen__draw_text(pixels, &render_framebuffer, 48U, 48U, "kernel panik", PANIC_SCREEN_RED);
    PanicScreen__draw_text(pixels, &render_framebuffer, 48U, 92U, "Hund tot", PANIC_SCREEN_FG);
    PanicScreen__draw_text(pixels, &render_framebuffer, 48U, 136U, message, PANIC_SCREEN_FG);
    PanicScreen__draw_dead_dog(pixels, &render_framebuffer);
}

static void Kernel__panic_screen(const char * message)
{
    BasicIO__puts(panic_dead_dog_ascii);
    KernelPtr kernel = Kernel__instance();
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            match_optional(
                system_info.framebuffer,
                some(framebuffer)
                {
                    PanicScreen__render(&framebuffer, message);
                },
                none_do_nothing()
            );
        },
        none_do_nothing()
    );
}

void Kernel__panic(const char * message, ProcessContextPtr context)
{
    Kernel__panic_screen(message);
    BasicIO__puts("Kernel panik: ");
    BasicIO__puts(message);
    BasicIO__puts("\nSystem halted\n");
    k_printf("Kernel panik: %s\n", message);
    k_println("System halted");

    Kernel__dump_state(context);
    CPU__halt_all();
}

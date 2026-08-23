#define DRIVER_LOG_TAG "VIDEO-TERM"

#include "../../sys/debug.h"
#include "../../sys/framebuffer_config.h"
#include "../../sys/io_server.h"
#include "../../sys/service.h"
#include "types/optional.h"
#include "emoji_atlas.h"
#include "spleen_font.h"

#ifndef VIDEO_TERM_ENTRYPOINT
#define VIDEO_TERM_ENTRYPOINT main
#endif

#define VIDEO_TERM_PATH ":console:"
#define VIDEO_TERM_FONT_WIDTH 16ULL
#define VIDEO_TERM_FONT_HEIGHT 16ULL
#define VIDEO_TERM_SCALE 1ULL
#define VIDEO_TERM_COLUMN_WIDTH (8ULL * VIDEO_TERM_SCALE)
#define VIDEO_TERM_CELL_HEIGHT VIDEO_TERM_FONT_HEIGHT
#define VIDEO_TERM_TEXT_Y_OFFSET ((VIDEO_TERM_CELL_HEIGHT - VIDEO_TERM_FONT_HEIGHT) / 2ULL)
#define VIDEO_TERM_COLOR_BG 0x00000000U
#define VIDEO_TERM_COLOR_FG 0x00d8d8d8U
#define VIDEO_TERM_CURSOR_XOR_COLOR 0x00ffffffU
#define VIDEO_TERM_REPLACEMENT_CODEPOINT 0xfffdU
#define VIDEO_TERM_EMOJI_CLUSTER_CAPACITY VIDEO_TERM_EMOJI_MAX_CODEPOINTS

#ifndef VIDEO_TERM_FONT_START
extern const uint8_t _binary_vendor_unifont_unifont_all_17_0_05_hex_start[];
#define VIDEO_TERM_FONT_START _binary_vendor_unifont_unifont_all_17_0_05_hex_start
#endif

#ifndef VIDEO_TERM_FONT_END
extern const uint8_t _binary_vendor_unifont_unifont_all_17_0_05_hex_end[];
#define VIDEO_TERM_FONT_END _binary_vendor_unifont_unifont_all_17_0_05_hex_end
#endif

#ifndef VIDEO_TERM_EMOJI_ATLAS_START
extern const uint8_t _binary_build_user_drivers_video_term_emoji_atlas_rgba_start[];
#define VIDEO_TERM_EMOJI_ATLAS_START _binary_build_user_drivers_video_term_emoji_atlas_rgba_start
#endif

#ifndef VIDEO_TERM_EMOJI_ATLAS_END
extern const uint8_t _binary_build_user_drivers_video_term_emoji_atlas_rgba_end[];
#define VIDEO_TERM_EMOJI_ATLAS_END _binary_build_user_drivers_video_term_emoji_atlas_rgba_end
#endif

typedef struct VideoTermGlyph
{
    uint64_t width;
    uint16_t rows[VIDEO_TERM_FONT_HEIGHT];
}
VideoTermGlyph;

typedef VideoTermGlyph * VideoTermGlyphPtr;
OptionalDef(VideoTermEmojiEntryPtr);

typedef enum VideoTermEscapeState
{
    VIDEO_TERM_ESCAPE_STATE_NONE,
    VIDEO_TERM_ESCAPE_STATE_ESC,
    VIDEO_TERM_ESCAPE_STATE_CSI,
}
VideoTermEscapeState;

typedef struct VideoTerm
{
    uint32_t * pixels;
    uint64_t mapped_pixels;
    FramebufferConfig config;
    uint64_t columns;
    uint64_t rows;
    uint64_t cursor_x;
    uint64_t cursor_y;
    bool cursor_visible;
    uint64_t cursor_draw_x;
    uint64_t cursor_draw_y;
    uint32_t utf8_codepoint;
    uint8_t utf8_remaining;
    VideoTermEscapeState escape_state;
    uint64_t csi_value;
    bool csi_has_value;
    uint32_t emoji_codepoints[VIDEO_TERM_EMOJI_CLUSTER_CAPACITY];
    uint8_t emoji_codepoint_count;
}
VideoTerm;

typedef VideoTerm * VideoTermPtr;

static void VideoTerm__newline(VideoTermPtr self);

static bool VideoTerm__is_hex_digit(uint8_t ch)
{
    return (ch >= '0' && ch <= '9')
        || (ch >= 'A' && ch <= 'F')
        || (ch >= 'a' && ch <= 'f');
}

static uint8_t VideoTerm__hex_value(uint8_t ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return (uint8_t) (ch - '0');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return (uint8_t) (10U + (ch - 'A'));
    }
    return (uint8_t) (10U + (ch - 'a'));
}

static const uint8_t * VideoTerm__next_line(const uint8_t * cursor, const uint8_t * end)
{
    while (cursor < end && *cursor != '\n')
    {
        cursor++;
    }
    if (cursor < end)
    {
        cursor++;
    }
    return cursor;
}

static bool VideoTerm__parse_codepoint(
    const uint8_t ** cursor,
    const uint8_t * end,
    uint32_t * codepoint)
{
    uint32_t value = 0;
    const uint8_t * current = *cursor;
    bool found_digit = false;

    while (current < end && *current != ':')
    {
        if (!VideoTerm__is_hex_digit(*current))
        {
            return false;
        }

        value = (value << 4U) | VideoTerm__hex_value(*current);
        found_digit = true;
        current++;
    }

    if (!found_digit || current >= end || *current != ':')
    {
        return false;
    }

    *cursor = current + 1;
    *codepoint = value;
    return true;
}

static bool VideoTerm__parse_glyph_rows(
    const uint8_t * cursor,
    const uint8_t * end,
    VideoTermGlyphPtr glyph)
{
    uint64_t hex_digits = 0;
    const uint8_t * current = cursor;

    while (current < end && *current != '\n' && *current != '\r')
    {
        if (!VideoTerm__is_hex_digit(*current))
        {
            return false;
        }
        hex_digits++;
        current++;
    }

    if (hex_digits != 32 && hex_digits != 64)
    {
        return false;
    }

    glyph->width = hex_digits == 32 ? 8 : 16;
    uint64_t digits_per_row = hex_digits / VIDEO_TERM_FONT_HEIGHT;

    for (uint64_t row = 0; row < VIDEO_TERM_FONT_HEIGHT; row++)
    {
        uint16_t row_bits = 0;
        for (uint64_t digit = 0; digit < digits_per_row; digit++)
        {
            row_bits = (uint16_t) ((row_bits << 4U) | VideoTerm__hex_value(cursor[(row * digits_per_row) + digit]));
        }
        if (glyph->width == 8)
        {
            row_bits = (uint16_t) (row_bits << 8U);
        }
        glyph->rows[row] = row_bits;
    }

    return true;
}

static bool VideoTerm__load_glyph(uint32_t codepoint, VideoTermGlyphPtr glyph)
{
    const uint8_t * cursor = VIDEO_TERM_FONT_START;
    const uint8_t * end = VIDEO_TERM_FONT_END;

    while (cursor < end)
    {
        const uint8_t * line_start = cursor;
        uint32_t line_codepoint = 0;

        if (!VideoTerm__parse_codepoint(&cursor, end, &line_codepoint))
        {
            cursor = VideoTerm__next_line(line_start, end);
            continue;
        }

        if (line_codepoint == codepoint)
        {
            return VideoTerm__parse_glyph_rows(cursor, end, glyph);
        }
        if (line_codepoint > codepoint)
        {
            return false;
        }

        cursor = VideoTerm__next_line(cursor, end);
    }

    return false;
}

static bool VideoTerm__load_primary_glyph(uint32_t codepoint, VideoTermGlyphPtr glyph)
{
    if (codepoint < VIDEO_TERM_SPLEEN_FIRST_CODEPOINT || codepoint > VIDEO_TERM_SPLEEN_LAST_CODEPOINT)
    {
        return false;
    }

    uint32_t glyph_index = codepoint - VIDEO_TERM_SPLEEN_FIRST_CODEPOINT;
    glyph->width = 8;
    for (uint64_t row = 0; row < VIDEO_TERM_FONT_HEIGHT; row++)
    {
        glyph->rows[row] = (uint16_t) (VIDEO_TERM_SPLEEN_GLYPHS[glyph_index][row] << 8U);
    }

    return true;
}

static bool VideoTerm__load_any_glyph(uint32_t codepoint, VideoTermGlyphPtr glyph)
{
    if (VideoTerm__load_primary_glyph(codepoint, glyph))
    {
        return true;
    }

    return VideoTerm__load_glyph(codepoint, glyph);
}

static bool VideoTerm__emoji_entry_matches(
    VideoTermEmojiEntryPtr entry,
    const uint32_t * codepoints,
    uint8_t codepoint_count)
{
    if (entry->codepoint_count != codepoint_count)
    {
        return false;
    }

    for (uint8_t index = 0; index < codepoint_count; index++)
    {
        if (entry->codepoints[index] != codepoints[index])
        {
            return false;
        }
    }

    return true;
}

static bool VideoTerm__emoji_entry_has_prefix(
    VideoTermEmojiEntryPtr entry,
    const uint32_t * codepoints,
    uint8_t codepoint_count)
{
    if (entry->codepoint_count < codepoint_count)
    {
        return false;
    }

    for (uint8_t index = 0; index < codepoint_count; index++)
    {
        if (entry->codepoints[index] != codepoints[index])
        {
            return false;
        }
    }

    return true;
}

static Optional(VideoTermEmojiEntryPtr) VideoTerm__find_emoji(
    const uint32_t * codepoints,
    uint8_t codepoint_count)
{
    for (uint64_t index = 0; index < VIDEO_TERM_EMOJI_ENTRY_COUNT; index++)
    {
        if (VideoTerm__emoji_entry_matches(&VIDEO_TERM_EMOJI_ENTRIES[index], codepoints, codepoint_count))
        {
            return Some(VideoTermEmojiEntryPtr, &VIDEO_TERM_EMOJI_ENTRIES[index]);
        }
    }

    return None(VideoTermEmojiEntryPtr);
}

static bool VideoTerm__has_emoji_prefix(const uint32_t * codepoints, uint8_t codepoint_count)
{
    for (uint64_t index = 0; index < VIDEO_TERM_EMOJI_ENTRY_COUNT; index++)
    {
        if (VideoTerm__emoji_entry_has_prefix(&VIDEO_TERM_EMOJI_ENTRIES[index], codepoints, codepoint_count))
        {
            return true;
        }
    }

    return false;
}

static uint32_t VideoTerm__blend_channel(uint32_t source, uint32_t target, uint32_t alpha)
{
    return ((source * alpha) + (target * (255U - alpha))) / 255U;
}

static uint32_t VideoTerm__blend_rgba(uint32_t target, uint8_t red, uint8_t green, uint8_t blue, uint8_t alpha)
{
    uint32_t target_red = (target >> 16U) & 0xffU;
    uint32_t target_green = (target >> 8U) & 0xffU;
    uint32_t target_blue = target & 0xffU;

    uint32_t out_red = VideoTerm__blend_channel(red, target_red, alpha);
    uint32_t out_green = VideoTerm__blend_channel(green, target_green, alpha);
    uint32_t out_blue = VideoTerm__blend_channel(blue, target_blue, alpha);

    return (out_red << 16U) | (out_green << 8U) | out_blue;
}

static uint32_t VideoTerm__mask_shift(uint64_t mask)
{
    uint32_t shift = 0;
    while (shift < 32 && ((mask >> shift) & 1ULL) == 0)
    {
        shift++;
    }

    return shift;
}

static uint32_t VideoTerm__mask_width(uint64_t mask)
{
    uint32_t shift = VideoTerm__mask_shift(mask);
    uint32_t width = 0;
    while ((shift + width) < 32 && ((mask >> (shift + width)) & 1ULL) != 0)
    {
        width++;
    }

    return width;
}

static uint32_t VideoTerm__pack_channel(uint8_t value, uint64_t mask)
{
    if (mask == 0)
    {
        return 0;
    }

    uint32_t shift = VideoTerm__mask_shift(mask);
    uint32_t width = VideoTerm__mask_width(mask);
    if (width == 0)
    {
        return 0;
    }

    uint32_t max_value = width == 32 ? 0xffffffffU : ((1U << width) - 1U);
    uint32_t scaled = ((uint32_t) value * max_value + 127U) / 255U;
    return (scaled << shift) & (uint32_t) mask;
}

static uint32_t VideoTerm__convert_bitmask_color(VideoTermPtr self, uint32_t rgb)
{
    uint8_t red = (uint8_t) ((rgb >> 16U) & 0xffU);
    uint8_t green = (uint8_t) ((rgb >> 8U) & 0xffU);
    uint8_t blue = (uint8_t) (rgb & 0xffU);

    return VideoTerm__pack_channel(red, self->config.red_mask)
        | VideoTerm__pack_channel(green, self->config.green_mask)
        | VideoTerm__pack_channel(blue, self->config.blue_mask);
}

static uint32_t VideoTerm__convert_color(VideoTermPtr self, uint32_t rgb)
{
    if (self->config.pixel_format == FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR)
    {
        uint32_t red = (rgb >> 16) & 0xffU;
        uint32_t green = (rgb >> 8) & 0xffU;
        uint32_t blue = rgb & 0xffU;
        return (blue << 16) | (green << 8) | red;
    }
    if (self->config.pixel_format == FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BIT_MASK)
    {
        return VideoTerm__convert_bitmask_color(self, rgb);
    }

    return rgb;
}

static void VideoTerm__put_pixel(VideoTermPtr self, uint64_t x, uint64_t y, uint32_t color)
{
    if (x >= self->config.width || y >= self->config.height)
    {
        return;
    }

    uint64_t pixel_index = (y * self->config.pixels_per_scanline) + x;
    if (pixel_index >= self->mapped_pixels)
    {
        return;
    }

    self->pixels[pixel_index] = VideoTerm__convert_color(self, color);
}

static void VideoTerm__fill_rect(
    VideoTermPtr self,
    uint64_t x,
    uint64_t y,
    uint64_t width,
    uint64_t height,
    uint32_t color)
{
    uint32_t converted_color = VideoTerm__convert_color(self, color);
    for (uint64_t row = 0; row < height; row++)
    {
        uint64_t target_y = y + row;
        if (target_y >= self->config.height)
        {
            return;
        }

        for (uint64_t col = 0; col < width; col++)
        {
            uint64_t target_x = x + col;
            if (target_x >= self->config.width)
            {
                break;
            }

            uint64_t pixel_index = (target_y * self->config.pixels_per_scanline) + target_x;
            if (pixel_index >= self->mapped_pixels)
            {
                break;
            }

            self->pixels[pixel_index] = converted_color;
        }
    }
}

static void VideoTerm__invert_cursor_cell(VideoTermPtr self, uint64_t cell_x, uint64_t cell_y)
{
    if (cell_x >= self->columns || cell_y >= self->rows)
    {
        return;
    }

    uint64_t origin_x = cell_x * VIDEO_TERM_COLUMN_WIDTH;
    uint64_t origin_y = cell_y * VIDEO_TERM_CELL_HEIGHT;
    uint32_t xor_color = VideoTerm__convert_color(self, VIDEO_TERM_CURSOR_XOR_COLOR);

    for (uint64_t row = 0; row < VIDEO_TERM_CELL_HEIGHT; row++)
    {
        uint64_t target_y = origin_y + row;
        if (target_y >= self->config.height)
        {
            return;
        }

        for (uint64_t col = 0; col < VIDEO_TERM_COLUMN_WIDTH; col++)
        {
            uint64_t target_x = origin_x + col;
            if (target_x >= self->config.width)
            {
                break;
            }

            uint64_t pixel_index = (target_y * self->config.pixels_per_scanline) + target_x;
            if (pixel_index >= self->mapped_pixels)
            {
                break;
            }

            self->pixels[pixel_index] ^= xor_color;
        }
    }
}

static void VideoTerm__show_cursor(VideoTermPtr self)
{
    if (self->cursor_visible)
    {
        return;
    }

    self->cursor_draw_x = self->cursor_x;
    self->cursor_draw_y = self->cursor_y;
    VideoTerm__invert_cursor_cell(self, self->cursor_draw_x, self->cursor_draw_y);
    self->cursor_visible = true;
}

static void VideoTerm__hide_cursor(VideoTermPtr self)
{
    if (!self->cursor_visible)
    {
        return;
    }

    VideoTerm__invert_cursor_cell(self, self->cursor_draw_x, self->cursor_draw_y);
    self->cursor_visible = false;
}

static void VideoTerm__draw_emoji(VideoTermPtr self, VideoTermEmojiEntryPtr emoji)
{
    if (self->cursor_x + VIDEO_TERM_EMOJI_ADVANCE_COLUMNS > self->columns)
    {
        VideoTerm__newline(self);
    }

    uint64_t origin_x = self->cursor_x * VIDEO_TERM_COLUMN_WIDTH;
    uint64_t origin_y = self->cursor_y * VIDEO_TERM_CELL_HEIGHT;
    uint64_t cell_width = VIDEO_TERM_EMOJI_ADVANCE_COLUMNS * VIDEO_TERM_COLUMN_WIDTH;
    const uint8_t * atlas = VIDEO_TERM_EMOJI_ATLAS_START;
    uint32_t atlas_size = (uint32_t) (VIDEO_TERM_EMOJI_ATLAS_END - VIDEO_TERM_EMOJI_ATLAS_START);
    uint32_t image_size = (uint32_t) (VIDEO_TERM_EMOJI_SIZE * VIDEO_TERM_EMOJI_SIZE * 4ULL);

    if (emoji->atlas_offset + image_size > atlas_size)
    {
        return;
    }

    VideoTerm__fill_rect(self, origin_x, origin_y, cell_width, VIDEO_TERM_CELL_HEIGHT, VIDEO_TERM_COLOR_BG);

    for (uint64_t row = 0; row < VIDEO_TERM_CELL_HEIGHT; row++)
    {
        uint64_t source_row = (row * VIDEO_TERM_EMOJI_SIZE) / VIDEO_TERM_CELL_HEIGHT;
        for (uint64_t col = 0; col < VIDEO_TERM_CELL_HEIGHT; col++)
        {
            uint64_t source_col = (col * VIDEO_TERM_EMOJI_SIZE) / VIDEO_TERM_CELL_HEIGHT;
            uint32_t offset = emoji->atlas_offset
                + (uint32_t) (((source_row * VIDEO_TERM_EMOJI_SIZE) + source_col) * 4ULL);
            uint8_t red = atlas[offset];
            uint8_t green = atlas[offset + 1];
            uint8_t blue = atlas[offset + 2];
            uint8_t alpha = atlas[offset + 3];
            uint32_t color = VideoTerm__blend_rgba(VIDEO_TERM_COLOR_BG, red, green, blue, alpha);
            VideoTerm__put_pixel(self, origin_x + col, origin_y + row, color);
        }
    }

    self->cursor_x += VIDEO_TERM_EMOJI_ADVANCE_COLUMNS;
    if (self->cursor_x >= self->columns)
    {
        VideoTerm__newline(self);
    }
}

static void VideoTerm__clear(VideoTermPtr self)
{
    VideoTerm__fill_rect(self, 0, 0, self->config.width, self->config.height, VIDEO_TERM_COLOR_BG);
    self->cursor_x = 0;
    self->cursor_y = 0;
    self->cursor_visible = false;
    self->cursor_draw_x = 0;
    self->cursor_draw_y = 0;
}

static void VideoTerm__clear_line_cells(VideoTermPtr self, uint64_t start_column, uint64_t end_column)
{
    if (start_column >= self->columns || end_column <= start_column)
    {
        return;
    }

    if (end_column > self->columns)
    {
        end_column = self->columns;
    }

    VideoTerm__fill_rect(
        self,
        start_column * VIDEO_TERM_COLUMN_WIDTH,
        self->cursor_y * VIDEO_TERM_CELL_HEIGHT,
        (end_column - start_column) * VIDEO_TERM_COLUMN_WIDTH,
        VIDEO_TERM_CELL_HEIGHT,
        VIDEO_TERM_COLOR_BG);
}

static void VideoTerm__clear_line(VideoTermPtr self, uint64_t mode)
{
    if (mode == 1)
    {
        VideoTerm__clear_line_cells(self, 0, self->cursor_x + 1);
        return;
    }
    if (mode == 2)
    {
        VideoTerm__clear_line_cells(self, 0, self->columns);
        return;
    }

    VideoTerm__clear_line_cells(self, self->cursor_x, self->columns);
}

static void VideoTerm__move_cursor_left(VideoTermPtr self, uint64_t count)
{
    if (count == 0)
    {
        count = 1;
    }

    if (count > self->cursor_x)
    {
        self->cursor_x = 0;
        return;
    }

    self->cursor_x -= count;
}

static void VideoTerm__move_cursor_right(VideoTermPtr self, uint64_t count)
{
    if (count == 0)
    {
        count = 1;
    }

    self->cursor_x += count;
    if (self->cursor_x >= self->columns)
    {
        self->cursor_x = self->columns - 1;
    }
}

static void VideoTerm__newline(VideoTermPtr self)
{
    self->cursor_x = 0;
    self->cursor_y++;
    if (self->cursor_y >= self->rows)
    {
        VideoTerm__clear(self);
    }
}

static void VideoTerm__draw_codepoint(VideoTermPtr self, uint32_t codepoint)
{
    VideoTermGlyph glyph;
    if (!VideoTerm__load_any_glyph(codepoint, &glyph))
    {
        if (!VideoTerm__load_any_glyph(VIDEO_TERM_REPLACEMENT_CODEPOINT, &glyph))
        {
            return;
        }
    }

    uint64_t advance_columns = glyph.width / 8ULL;
    if (advance_columns == 0)
    {
        advance_columns = 1;
    }
    if (self->cursor_x + advance_columns > self->columns)
    {
        VideoTerm__newline(self);
    }

    uint64_t origin_x = self->cursor_x * VIDEO_TERM_COLUMN_WIDTH;
    uint64_t origin_y = self->cursor_y * VIDEO_TERM_CELL_HEIGHT;
    uint64_t cell_width = advance_columns * VIDEO_TERM_COLUMN_WIDTH;

    VideoTerm__fill_rect(self, origin_x, origin_y, cell_width, VIDEO_TERM_CELL_HEIGHT, VIDEO_TERM_COLOR_BG);

    for (uint64_t row = 0; row < VIDEO_TERM_FONT_HEIGHT; row++)
    {
        for (uint64_t col = 0; col < VIDEO_TERM_FONT_WIDTH; col++)
        {
            uint16_t mask = (uint16_t) (1U << (VIDEO_TERM_FONT_WIDTH - 1U - col));
            if ((glyph.rows[row] & mask) != 0)
            {
                VideoTerm__fill_rect(
                    self,
                    origin_x + (col * VIDEO_TERM_SCALE),
                    origin_y + VIDEO_TERM_TEXT_Y_OFFSET + (row * VIDEO_TERM_SCALE),
                    VIDEO_TERM_SCALE,
                    VIDEO_TERM_SCALE,
                    VIDEO_TERM_COLOR_FG);
            }
        }
    }

    self->cursor_x += advance_columns;
    if (self->cursor_x >= self->columns)
    {
        VideoTerm__newline(self);
    }
}

static bool VideoTerm__emoji_cluster_can_append(VideoTermPtr self, uint32_t codepoint)
{
    if (self->emoji_codepoint_count >= VIDEO_TERM_EMOJI_CLUSTER_CAPACITY)
    {
        return false;
    }

    uint32_t codepoints[VIDEO_TERM_EMOJI_CLUSTER_CAPACITY];
    for (uint8_t index = 0; index < self->emoji_codepoint_count; index++)
    {
        codepoints[index] = self->emoji_codepoints[index];
    }

    codepoints[self->emoji_codepoint_count] = codepoint;
    return VideoTerm__has_emoji_prefix(codepoints, self->emoji_codepoint_count + 1);
}

static bool VideoTerm__codepoint_can_start_emoji(uint32_t codepoint)
{
    uint32_t codepoints[1] = { codepoint };
    return VideoTerm__has_emoji_prefix(codepoints, 1);
}

static void VideoTerm__append_emoji_codepoint(VideoTermPtr self, uint32_t codepoint)
{
    if (self->emoji_codepoint_count < VIDEO_TERM_EMOJI_CLUSTER_CAPACITY)
    {
        self->emoji_codepoints[self->emoji_codepoint_count] = codepoint;
        self->emoji_codepoint_count++;
    }
}

static void VideoTerm__clear_emoji_cluster(VideoTermPtr self)
{
    self->emoji_codepoint_count = 0;
}

static bool VideoTerm__codepoint_prefers_text_presentation(uint32_t codepoint)
{
    return (codepoint >= '0' && codepoint <= '9') || codepoint == '#' || codepoint == '*';
}

static void VideoTerm__flush_emoji_cluster(VideoTermPtr self)
{
    if (self->emoji_codepoint_count == 0)
    {
        return;
    }

    if (self->emoji_codepoint_count == 1
        && VideoTerm__codepoint_prefers_text_presentation(self->emoji_codepoints[0]))
    {
        uint32_t codepoint = self->emoji_codepoints[0];
        VideoTerm__clear_emoji_cluster(self);
        VideoTerm__draw_codepoint(self, codepoint);
        return;
    }

    match_optional(VideoTermEmojiEntryPtr, VideoTerm__find_emoji(self->emoji_codepoints, self->emoji_codepoint_count),
        some(emoji)
        {
            VideoTerm__draw_emoji(self, emoji);
            VideoTerm__clear_emoji_cluster(self);
            return;
        },
        none()
        {
            uint8_t count = self->emoji_codepoint_count;
            uint32_t codepoints[VIDEO_TERM_EMOJI_CLUSTER_CAPACITY];
            for (uint8_t index = 0; index < count; index++)
            {
                codepoints[index] = self->emoji_codepoints[index];
            }
            VideoTerm__clear_emoji_cluster(self);
            for (uint8_t index = 0; index < count; index++)
            {
                VideoTerm__draw_codepoint(self, codepoints[index]);
            }
        }
    )
}

static void VideoTerm__write_text_or_emoji_codepoint(VideoTermPtr self, uint32_t codepoint)
{
    if (self->emoji_codepoint_count > 0)
    {
        if (VideoTerm__emoji_cluster_can_append(self, codepoint))
        {
            VideoTerm__append_emoji_codepoint(self, codepoint);
            return;
        }

        VideoTerm__flush_emoji_cluster(self);
    }

    if (VideoTerm__codepoint_can_start_emoji(codepoint))
    {
        VideoTerm__append_emoji_codepoint(self, codepoint);
        return;
    }

    VideoTerm__draw_codepoint(self, codepoint);
}

static void VideoTerm__write_codepoint(VideoTermPtr self, uint32_t codepoint)
{
    if (codepoint == '\n')
    {
        VideoTerm__flush_emoji_cluster(self);
        VideoTerm__newline(self);
        return;
    }
    if (codepoint == '\r')
    {
        VideoTerm__flush_emoji_cluster(self);
        self->cursor_x = 0;
        return;
    }
    if (codepoint == '\t')
    {
        VideoTerm__flush_emoji_cluster(self);
        for (uint64_t i = 0; i < 4; i++)
        {
            VideoTerm__draw_codepoint(self, ' ');
        }
        return;
    }
    if (codepoint < ' ')
    {
        VideoTerm__flush_emoji_cluster(self);
        return;
    }

    VideoTerm__write_text_or_emoji_codepoint(self, codepoint);
}

static void VideoTerm__reset_escape_sequence(VideoTermPtr self)
{
    self->escape_state = VIDEO_TERM_ESCAPE_STATE_NONE;
    self->csi_value = 0;
    self->csi_has_value = false;
}

static uint64_t VideoTerm__csi_value_or_default(VideoTermPtr self, uint64_t default_value)
{
    if (self->csi_has_value)
    {
        return self->csi_value;
    }

    return default_value;
}

static void VideoTerm__execute_csi(VideoTermPtr self, uint8_t command)
{
    if (command == 'K')
    {
        VideoTerm__clear_line(self, VideoTerm__csi_value_or_default(self, 0));
    }
    else if (command == 'D')
    {
        VideoTerm__move_cursor_left(self, VideoTerm__csi_value_or_default(self, 1));
    }
    else if (command == 'C')
    {
        VideoTerm__move_cursor_right(self, VideoTerm__csi_value_or_default(self, 1));
    }
}

static bool VideoTerm__consume_escape_byte(VideoTermPtr self, uint8_t byte)
{
    if (self->escape_state == VIDEO_TERM_ESCAPE_STATE_NONE)
    {
        if (byte == '\033')
        {
            VideoTerm__flush_emoji_cluster(self);
            self->utf8_codepoint = 0;
            self->utf8_remaining = 0;
            self->escape_state = VIDEO_TERM_ESCAPE_STATE_ESC;
            return true;
        }

        return false;
    }

    if (self->escape_state == VIDEO_TERM_ESCAPE_STATE_ESC)
    {
        if (byte == '[')
        {
            self->escape_state = VIDEO_TERM_ESCAPE_STATE_CSI;
            self->csi_value = 0;
            self->csi_has_value = false;
            return true;
        }

        VideoTerm__reset_escape_sequence(self);
        return true;
    }

    if (byte >= '0' && byte <= '9')
    {
        self->csi_value = (self->csi_value * 10ULL) + (uint64_t) (byte - '0');
        self->csi_has_value = true;
        return true;
    }

    if (byte >= 0x40U && byte <= 0x7eU)
    {
        VideoTerm__execute_csi(self, byte);
        VideoTerm__reset_escape_sequence(self);
        return true;
    }

    VideoTerm__reset_escape_sequence(self);
    return true;
}

static void VideoTerm__write_byte(VideoTermPtr self, uint8_t byte)
{
    if (VideoTerm__consume_escape_byte(self, byte))
    {
        return;
    }

    if (self->utf8_remaining == 0)
    {
        if ((byte & 0x80U) == 0)
        {
            VideoTerm__write_codepoint(self, byte);
            return;
        }
        if ((byte & 0xe0U) == 0xc0U)
        {
            self->utf8_codepoint = byte & 0x1fU;
            self->utf8_remaining = 1;
            return;
        }
        if ((byte & 0xf0U) == 0xe0U)
        {
            self->utf8_codepoint = byte & 0x0fU;
            self->utf8_remaining = 2;
            return;
        }
        if ((byte & 0xf8U) == 0xf0U)
        {
            self->utf8_codepoint = byte & 0x07U;
            self->utf8_remaining = 3;
            return;
        }

        VideoTerm__write_codepoint(self, VIDEO_TERM_REPLACEMENT_CODEPOINT);
        return;
    }

    if ((byte & 0xc0U) != 0x80U)
    {
        self->utf8_remaining = 0;
        self->utf8_codepoint = 0;
        VideoTerm__write_codepoint(self, VIDEO_TERM_REPLACEMENT_CODEPOINT);
        VideoTerm__write_byte(self, byte);
        return;
    }

    self->utf8_codepoint = (self->utf8_codepoint << 6U) | (byte & 0x3fU);
    self->utf8_remaining--;
    if (self->utf8_remaining == 0)
    {
        VideoTerm__write_codepoint(self, self->utf8_codepoint);
        self->utf8_codepoint = 0;
    }
}

static VideoTerm VideoTerm__new(FramebufferConfig config, void * pixels, uint64_t mapped_size)
{
    VideoTerm self = {
        .pixels = (uint32_t *) pixels,
        .mapped_pixels = mapped_size / sizeof(uint32_t),
        .config = config,
        .columns = config.width / VIDEO_TERM_COLUMN_WIDTH,
        .rows = config.height / VIDEO_TERM_CELL_HEIGHT,
        .cursor_x = 0,
        .cursor_y = 0,
        .cursor_visible = false,
        .cursor_draw_x = 0,
        .cursor_draw_y = 0,
        .utf8_codepoint = 0,
        .utf8_remaining = 0,
        .escape_state = VIDEO_TERM_ESCAPE_STATE_NONE,
        .csi_value = 0,
        .csi_has_value = false,
        .emoji_codepoints = { 0 },
        .emoji_codepoint_count = 0,
    };

    if (self.columns == 0)
    {
        self.columns = 1;
    }
    if (self.rows == 0)
    {
        self.rows = 1;
    }

    VideoTerm__clear(&self);
    return self;
}

int VIDEO_TERM_ENTRYPOINT(void)
{
    if (!service_ok(service_become_service()))
    {
        DRIVER_DEBUG("video_term: service registration failed\n");
        return 1;
    }

    FramebufferConfig config = { 0 };
    if (!FramebufferConfig__load(&config))
    {
        DRIVER_DEBUG("video_term: framebuffer config not present\n");
        return 1;
    }

    if (config.pixel_format == FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BLT_ONLY)
    {
        DRIVER_DEBUG("video_term: unsupported blt-only framebuffer\n");
        return 1;
    }

    ServiceMappedMemoryPtr framebuffer_mapping;
    if (!service_ok(service_map_mmio(config.physical_base, config.physical_size, &framebuffer_mapping)))
    {
        DRIVER_DEBUG("video_term: framebuffer map failed\n");
        return 1;
    }

    VideoTerm terminal = VideoTerm__new(config, driver_ptr(framebuffer_mapping), mapped_size(framebuffer_mapping));

    IOHandle video = io_server_register(VIDEO_TERM_PATH, IO_SERVER_WRITE);
    if (video < 0)
    {
        DRIVER_DEBUG("video_term: register error %i\n", (int) video);
        return 1;
    }

    VideoTerm__show_cursor(&terminal);

    ServiceEvent event;
    ServiceCode wait_status;
    for (wait_status = service_wait(&event);
         service_alive(wait_status);
         wait_status = service_wait(&event))
    {
        if (service_ok(wait_status))
        {
            bool write_request = event.type == SERVICE_EVENT_IO && event.source == SERVICE_IO_WRITE_REQUEST;
            if (write_request)
            {
                IOServerBuffer buffer;
                if (io_server_get_buffer(video, &buffer) == IO_OK)
                {
                    VideoTerm__hide_cursor(&terminal);
                    for (uint64_t i = 0; i < buffer.capacity; i++)
                    {
                        VideoTerm__write_byte(&terminal, (uint8_t) buffer.data[i]);
                    }
                    VideoTerm__flush_emoji_cluster(&terminal);
                    VideoTerm__show_cursor(&terminal);
                    io_server_data_consumed(video, buffer.capacity);
                }
            }
        }
    }

    return 0;
}

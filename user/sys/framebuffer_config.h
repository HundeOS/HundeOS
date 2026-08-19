#ifndef __USER_SYS_FRAMEBUFFER_CONFIG_H
#define __USER_SYS_FRAMEBUFFER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "io.h"

#define FRAMEBUFFER_CONFIG_PATH "/proc/sysinfo"
#define FRAMEBUFFER_CONFIG_SYSINFO_SIZE 16384

typedef enum FramebufferConfigPixelFormat
{
    FRAMEBUFFER_CONFIG_PIXEL_FORMAT_RGB_RESERVED_8BIT_PER_COLOR = 0,
    FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BGR_RESERVED_8BIT_PER_COLOR = 1,
    FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BIT_MASK = 2,
    FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BLT_ONLY = 3,
    FRAMEBUFFER_CONFIG_PIXEL_FORMAT_UNKNOWN = 0xff,
}
FramebufferConfigPixelFormat;

typedef struct FramebufferConfig
{
    uint64_t physical_base;
    uint64_t physical_size;
    uint64_t width;
    uint64_t height;
    uint64_t pixels_per_scanline;
    FramebufferConfigPixelFormat pixel_format;
    uint64_t red_mask;
    uint64_t green_mask;
    uint64_t blue_mask;
    uint64_t reserved_mask;
}
FramebufferConfig;

typedef FramebufferConfig * FramebufferConfigPtr;

static uint64_t FramebufferConfig__digit_value(char ch)
{
    if (ch >= '0' && ch <= '9')
    {
        return (uint64_t) (ch - '0');
    }
    if (ch >= 'a' && ch <= 'f')
    {
        return (uint64_t) (10 + ch - 'a');
    }
    if (ch >= 'A' && ch <= 'F')
    {
        return (uint64_t) (10 + ch - 'A');
    }

    return 16;
}

static Optional(uint64_t) FramebufferConfig__parse_uint64(ConstBStringPtr text)
{
    uint64_t base = 10;
    uint64_t parsed = 0;
    bool saw_digit = false;
    BString stripped = BString__strip(text);

    Match(char, BString__char_at(&stripped, 0))
        IsSome(first,
        {
            Match(char, BString__char_at(&stripped, 1))
                IsSome(second,
                {
                    if (first == '0' && (second == 'x' || second == 'X'))
                    {
                        base = 16;
                        BString without_prefix = BString__slice(&stripped, 2, -1);
                        stripped = without_prefix;
                    }
                })
            End
        })
    End

    BStringIterate(&stripped, index, ch)
        uint64_t digit = FramebufferConfig__digit_value(ch);
        if (digit >= base)
        {
            return None(uint64_t);
        }
        parsed = (parsed * base) + digit;
        saw_digit = true;
    }

    if (!saw_digit)
    {
        return None(uint64_t);
    }

    return Some(uint64_t, parsed);
}

static bool FramebufferConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, uint64_t * value)
{
    BString delimiter = BSTRING_NEW("=");
    BStringSplit split = BString__split_first_any(line, &delimiter);
    bool matched = false;

    Match(BString, split.match)
        IsSome(raw_key,
        {
            BString line_key = BString__strip(&raw_key);
            matched = BString__equals(&line_key, key);
        })
    End

    if (!matched)
    {
        return false;
    }

    Match(uint64_t, FramebufferConfig__parse_uint64(&split.rest))
        IsSome(parsed,
        {
            *value = parsed;
            return true;
        })
    End

    return false;
}

static void FramebufferConfig__read_config_line(
    ConstBStringPtr line,
    FramebufferConfigPtr self,
    bool * found_present,
    bool * found_physical_base,
    bool * found_physical_size,
    bool * found_width,
    bool * found_height,
    bool * found_pixels_per_scanline,
    bool * found_pixel_format,
    bool * found_red_mask,
    bool * found_green_mask,
    bool * found_blue_mask,
    bool * found_reserved_mask)
{
    BString present_key = BSTRING_NEW("framebuffer.present");
    BString physical_base_key = BSTRING_NEW("framebuffer.physical_base");
    BString physical_size_key = BSTRING_NEW("framebuffer.physical_size");
    BString width_key = BSTRING_NEW("framebuffer.width");
    BString height_key = BSTRING_NEW("framebuffer.height");
    BString pixels_per_scanline_key = BSTRING_NEW("framebuffer.pixels_per_scanline");
    BString pixel_format_key = BSTRING_NEW("framebuffer.pixel_format");
    BString red_mask_key = BSTRING_NEW("framebuffer.red_mask");
    BString green_mask_key = BSTRING_NEW("framebuffer.green_mask");
    BString blue_mask_key = BSTRING_NEW("framebuffer.blue_mask");
    BString reserved_mask_key = BSTRING_NEW("framebuffer.reserved_mask");
    uint64_t parsed = 0;

    if (!*found_present && FramebufferConfig__read_line_value(line, &present_key, &parsed))
    {
        *found_present = parsed == 1;
    }
    if (!*found_physical_base && FramebufferConfig__read_line_value(line, &physical_base_key, &self->physical_base))
    {
        *found_physical_base = true;
    }
    if (!*found_physical_size && FramebufferConfig__read_line_value(line, &physical_size_key, &self->physical_size))
    {
        *found_physical_size = true;
    }
    if (!*found_width && FramebufferConfig__read_line_value(line, &width_key, &self->width))
    {
        *found_width = true;
    }
    if (!*found_height && FramebufferConfig__read_line_value(line, &height_key, &self->height))
    {
        *found_height = true;
    }
    if (!*found_pixels_per_scanline
        && FramebufferConfig__read_line_value(line, &pixels_per_scanline_key, &self->pixels_per_scanline))
    {
        *found_pixels_per_scanline = true;
    }
    if (!*found_pixel_format && FramebufferConfig__read_line_value(line, &pixel_format_key, &parsed))
    {
        self->pixel_format = (FramebufferConfigPixelFormat) parsed;
        *found_pixel_format = true;
    }
    if (!*found_red_mask && FramebufferConfig__read_line_value(line, &red_mask_key, &self->red_mask))
    {
        *found_red_mask = true;
    }
    if (!*found_green_mask && FramebufferConfig__read_line_value(line, &green_mask_key, &self->green_mask))
    {
        *found_green_mask = true;
    }
    if (!*found_blue_mask && FramebufferConfig__read_line_value(line, &blue_mask_key, &self->blue_mask))
    {
        *found_blue_mask = true;
    }
    if (!*found_reserved_mask && FramebufferConfig__read_line_value(line, &reserved_mask_key, &self->reserved_mask))
    {
        *found_reserved_mask = true;
    }
}

static bool FramebufferConfig__has_required_fields(
    FramebufferConfigPtr self,
    bool found_present,
    bool found_physical_base,
    bool found_physical_size,
    bool found_width,
    bool found_height,
    bool found_pixels_per_scanline,
    bool found_pixel_format,
    bool found_red_mask,
    bool found_green_mask,
    bool found_blue_mask,
    bool found_reserved_mask)
{
    bool found_required_config = found_present
        && found_physical_base
        && found_physical_size
        && found_width
        && found_height
        && found_pixels_per_scanline
        && found_pixel_format;

    if (!found_required_config)
    {
        return false;
    }

    if (self->pixel_format == FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BIT_MASK)
    {
        return found_red_mask
            && found_green_mask
            && found_blue_mask
            && found_reserved_mask;
    }

    return true;
}

static bool FramebufferConfig__read_config(char * sysinfo, ByteCount bytes, FramebufferConfigPtr self)
{
    BString line = EMPTY_BSTRING;
    bool found_present = false;
    bool found_physical_base = false;
    bool found_physical_size = false;
    bool found_width = false;
    bool found_height = false;
    bool found_pixels_per_scanline = false;
    bool found_pixel_format = false;
    bool found_red_mask = false;
    bool found_green_mask = false;
    bool found_blue_mask = false;
    bool found_reserved_mask = false;

    for (ByteCount index = 0; index < bytes; index++)
    {
        if (sysinfo[index] == '\n')
        {
            FramebufferConfig__read_config_line(
                &line,
                self,
                &found_present,
                &found_physical_base,
                &found_physical_size,
                &found_width,
                &found_height,
                &found_pixels_per_scanline,
                &found_pixel_format,
                &found_red_mask,
                &found_green_mask,
                &found_blue_mask,
                &found_reserved_mask);
            BString__clear(&line);
        }
        else if (!BString__push_char(&line, sysinfo[index]))
        {
            BString__clear(&line);
        }

        if (FramebufferConfig__has_required_fields(
            self,
            found_present,
            found_physical_base,
            found_physical_size,
            found_width,
            found_height,
            found_pixels_per_scanline,
            found_pixel_format,
            found_red_mask,
            found_green_mask,
            found_blue_mask,
            found_reserved_mask))
        {
            return true;
        }
    }

    if (!BString__is_empty(&line))
    {
        FramebufferConfig__read_config_line(
            &line,
            self,
            &found_present,
            &found_physical_base,
            &found_physical_size,
            &found_width,
            &found_height,
            &found_pixels_per_scanline,
            &found_pixel_format,
            &found_red_mask,
            &found_green_mask,
            &found_blue_mask,
            &found_reserved_mask);
    }

    return FramebufferConfig__has_required_fields(
        self,
        found_present,
        found_physical_base,
        found_physical_size,
        found_width,
        found_height,
        found_pixels_per_scanline,
        found_pixel_format,
        found_red_mask,
        found_green_mask,
        found_blue_mask,
        found_reserved_mask);
}

static bool FramebufferConfig__load(FramebufferConfigPtr self)
{
    char sysinfo[FRAMEBUFFER_CONFIG_SYSINFO_SIZE];
    IOHandle handle = open(FRAMEBUFFER_CONFIG_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    ByteCount bytes = read(handle, sysinfo, sizeof(sysinfo) - 1);
    close(handle);
    if (bytes <= 0)
    {
        return false;
    }

    return FramebufferConfig__read_config(sysinfo, bytes, self);
}

#endif

#ifndef __USER_SYS_FRAMEBUFFER_CONFIG_H
#define __USER_SYS_FRAMEBUFFER_CONFIG_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../../common/types/bstring.h"
#include "../../common/types/dyn_string.h"
#include "io.h"

#define FRAMEBUFFER_CONFIG_PATH "/proc/sysinfo"
#define FRAMEBUFFER_CONFIG_MAX 16384

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

static const BString framebuffer_config_delimiter = BSTRING_NEW("=");
static const BString framebuffer_config_present_key = BSTRING_NEW("framebuffer.present");
static const BString framebuffer_config_physical_base_key = BSTRING_NEW("framebuffer.physical_base");
static const BString framebuffer_config_physical_size_key = BSTRING_NEW("framebuffer.physical_size");
static const BString framebuffer_config_width_key = BSTRING_NEW("framebuffer.width");
static const BString framebuffer_config_height_key = BSTRING_NEW("framebuffer.height");
static const BString framebuffer_config_pixels_per_scanline_key = BSTRING_NEW("framebuffer.pixels_per_scanline");
static const BString framebuffer_config_pixel_format_key = BSTRING_NEW("framebuffer.pixel_format");
static const BString framebuffer_config_red_mask_key = BSTRING_NEW("framebuffer.red_mask");
static const BString framebuffer_config_green_mask_key = BSTRING_NEW("framebuffer.green_mask");
static const BString framebuffer_config_blue_mask_key = BSTRING_NEW("framebuffer.blue_mask");
static const BString framebuffer_config_reserved_mask_key = BSTRING_NEW("framebuffer.reserved_mask");

static bool FramebufferConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key, uint64_t * value)
{
    BStringSplit split = BString__split_first_any(line, &framebuffer_config_delimiter);
    bool matched = false;

    match_optional(BString, split.match,
        some(raw_key)
        {
            BString line_key = BString__strip(&raw_key);
            matched = BString__equals(&line_key, key);
        },
        none() {}
    )

    if (!matched)
    {
        return false;
    }

    match_optional(uint64_t, BString__parse_uint64_base(&split.rest, 0),
        some(parsed)
        {
            *value = parsed;
            return true;
        },
        none() {}
    )

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
    uint64_t parsed = 0;

    if (!*found_present && FramebufferConfig__read_line_value(line, &framebuffer_config_present_key, &parsed))
    {
        *found_present = parsed == 1;
    }
    if (!*found_physical_base && FramebufferConfig__read_line_value(line, &framebuffer_config_physical_base_key, &self->physical_base))
    {
        *found_physical_base = true;
    }
    if (!*found_physical_size && FramebufferConfig__read_line_value(line, &framebuffer_config_physical_size_key, &self->physical_size))
    {
        *found_physical_size = true;
    }
    if (!*found_width && FramebufferConfig__read_line_value(line, &framebuffer_config_width_key, &self->width))
    {
        *found_width = true;
    }
    if (!*found_height && FramebufferConfig__read_line_value(line, &framebuffer_config_height_key, &self->height))
    {
        *found_height = true;
    }
    if (!*found_pixels_per_scanline
        && FramebufferConfig__read_line_value(line, &framebuffer_config_pixels_per_scanline_key, &self->pixels_per_scanline))
    {
        *found_pixels_per_scanline = true;
    }
    if (!*found_pixel_format && FramebufferConfig__read_line_value(line, &framebuffer_config_pixel_format_key, &parsed))
    {
        self->pixel_format = (FramebufferConfigPixelFormat) parsed;
        *found_pixel_format = true;
    }
    if (!*found_red_mask && FramebufferConfig__read_line_value(line, &framebuffer_config_red_mask_key, &self->red_mask))
    {
        *found_red_mask = true;
    }
    if (!*found_green_mask && FramebufferConfig__read_line_value(line, &framebuffer_config_green_mask_key, &self->green_mask))
    {
        *found_green_mask = true;
    }
    if (!*found_blue_mask && FramebufferConfig__read_line_value(line, &framebuffer_config_blue_mask_key, &self->blue_mask))
    {
        *found_blue_mask = true;
    }
    if (!*found_reserved_mask && FramebufferConfig__read_line_value(line, &framebuffer_config_reserved_mask_key, &self->reserved_mask))
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

static bool FramebufferConfig__read_config(ConstDynStringPtr sysinfo, FramebufferConfigPtr self)
{
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

    match_optional(DynStringLines, DynString__lines(sysinfo),
        some(lines)
        {
            LLIterate(DynStringLines, lines, line)
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line)
                    {
                        FramebufferConfig__read_config_line(
                        &bstring_line,
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
                        &found_reserved_mask
                    ); },
                    none() {}
                )

            End
            DynStringLines__dispose_all(&lines);
        },
        none() {}
    )

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
    IOHandle handle = open(FRAMEBUFFER_CONFIG_PATH, IO_OPEN_READ);
    if (handle < 0)
    {
        return false;
    }

    Optional(DynString) sysinfo = IO__read_dyn_string(handle, FRAMEBUFFER_CONFIG_MAX);
    close(handle);

    match_optional(DynString, sysinfo,
        some(data)
        {
            bool loaded = FramebufferConfig__read_config(&data, self);
            DynString__dispose(&data);
            return loaded;
        },
        none() {}
    )

    return false;
}

#endif

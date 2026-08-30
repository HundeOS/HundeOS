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

static FramebufferConfig FramebufferConfig__new(void)
{
    return (FramebufferConfig) { 0 };
}

typedef struct FramebufferConfigFields
{
    bool present;
    bool physical_base;
    bool physical_size;
    bool width;
    bool height;
    bool pixels_per_scanline;
    bool pixel_format;
    bool red_mask;
    bool green_mask;
    bool blue_mask;
    bool reserved_mask;
}
FramebufferConfigFields;

typedef FramebufferConfigFields * FramebufferConfigFieldsPtr;

static const BString framebuffer_config_delimiter = bstr("=");
static const BString framebuffer_config_present_key = bstr("framebuffer.present");
static const BString framebuffer_config_physical_base_key = bstr("framebuffer.physical_base");
static const BString framebuffer_config_physical_size_key = bstr("framebuffer.physical_size");
static const BString framebuffer_config_width_key = bstr("framebuffer.width");
static const BString framebuffer_config_height_key = bstr("framebuffer.height");
static const BString framebuffer_config_pixels_per_scanline_key = bstr("framebuffer.pixels_per_scanline");
static const BString framebuffer_config_pixel_format_key = bstr("framebuffer.pixel_format");
static const BString framebuffer_config_red_mask_key = bstr("framebuffer.red_mask");
static const BString framebuffer_config_green_mask_key = bstr("framebuffer.green_mask");
static const BString framebuffer_config_blue_mask_key = bstr("framebuffer.blue_mask");
static const BString framebuffer_config_reserved_mask_key = bstr("framebuffer.reserved_mask");

static FramebufferConfigFields FramebufferConfigFields__new()
{
    return (FramebufferConfigFields) { 0 };
}

static Optional(uint64_t) FramebufferConfig__read_line_value(ConstBStringPtr line, ConstBStringPtr key)
{
    BStringSplit split = BString__split_first_any(line, &framebuffer_config_delimiter);
    bool matched = false;

    match_optional(BString, split.match,
        some(raw_key)
        {
            BString line_key = BString__strip(&raw_key);
            matched = BString__equals(&line_key, key);
        },
        none_do_nothing()
    );

    if (!matched)
    {
        return None(uint64_t);
    }

    return BString__parse_uint64_base(&split.rest, 0);
}

static void FramebufferConfigFields__read_config_line(
    FramebufferConfigFieldsPtr self,
    ConstBStringPtr line,
    FramebufferConfigPtr config)
{
    if (!self->present)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_present_key),
            some(parsed)
            {
                self->present = parsed == 1;
            },
            none_do_nothing()
        );
    }
    if (!self->physical_base)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_physical_base_key),
            some(parsed)
            {
                config->physical_base = parsed;
                self->physical_base = true;
            },
            none_do_nothing()
        );
    }
    if (!self->physical_size)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_physical_size_key),
            some(parsed)
            {
                config->physical_size = parsed;
                self->physical_size = true;
            },
            none_do_nothing()
        );
    }
    if (!self->width)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_width_key),
            some(parsed)
            {
                config->width = parsed;
                self->width = true;
            },
            none_do_nothing()
        );
    }
    if (!self->height)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_height_key),
            some(parsed)
            {
                config->height = parsed;
                self->height = true;
            },
            none_do_nothing()
        );
    }
    if (!self->pixels_per_scanline)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_pixels_per_scanline_key),
            some(parsed)
            {
                config->pixels_per_scanline = parsed;
                self->pixels_per_scanline = true;
            },
            none_do_nothing()
        );
    }
    if (!self->pixel_format)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_pixel_format_key),
            some(parsed)
            {
                config->pixel_format = (FramebufferConfigPixelFormat) parsed;
                self->pixel_format = true;
            },
            none_do_nothing()
        );
    }
    if (!self->red_mask)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_red_mask_key),
            some(parsed)
            {
                config->red_mask = parsed;
                self->red_mask = true;
            },
            none_do_nothing()
        );
    }
    if (!self->green_mask)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_green_mask_key),
            some(parsed)
            {
                config->green_mask = parsed;
                self->green_mask = true;
            },
            none_do_nothing()
        );
    }
    if (!self->blue_mask)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_blue_mask_key),
            some(parsed)
            {
                config->blue_mask = parsed;
                self->blue_mask = true;
            },
            none_do_nothing()
        );
    }
    if (!self->reserved_mask)
    {
        match_optional(uint64_t, FramebufferConfig__read_line_value(line, &framebuffer_config_reserved_mask_key),
            some(parsed)
            {
                config->reserved_mask = parsed;
                self->reserved_mask = true;
            },
            none_do_nothing()
        );
    }
}

static bool FramebufferConfig__has_required_fields(
    FramebufferConfigPtr self,
    FramebufferConfigFields fields)
{
    bool found_required_config = fields.present
        && fields.physical_base
        && fields.physical_size
        && fields.width
        && fields.height
        && fields.pixels_per_scanline
        && fields.pixel_format;

    if (!found_required_config)
    {
        return false;
    }

    if (self->pixel_format == FRAMEBUFFER_CONFIG_PIXEL_FORMAT_BIT_MASK)
    {
        return fields.red_mask
            && fields.green_mask
            && fields.blue_mask
            && fields.reserved_mask;
    }

    return true;
}

static bool FramebufferConfig__read_config(ConstDynStringPtr sysinfo, FramebufferConfigPtr self)
{
    FramebufferConfigFields fields = FramebufferConfigFields__new();

    match_optional(DynStringLines, DynString__lines(sysinfo),
        some(lines)
        {
            for_each_ll(DynStringLines, lines, line,
            {
                match_optional(BString, DynString__bstring(&line),
                    some(bstring_line)
                    {
                        FramebufferConfigFields__read_config_line(&fields, &bstring_line, self);
                    },
                    none_do_nothing()
                );
            });
            DynStringLines__dispose_all(&lines);
        },
        none_do_nothing()
    );

    return FramebufferConfig__has_required_fields(self, fields);
}

static bool FramebufferConfig__load(FramebufferConfigPtr self)
{
    BString config_path = bstr(FRAMEBUFFER_CONFIG_PATH);
    IOHandle handle = open(&config_path, IO_OPEN_READ);
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
        none_do_nothing()
    );

    return false;
}

#endif

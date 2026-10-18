#ifndef __KERNEL_ARTWORK_H
#define __KERNEL_ARTWORK_H

#include <stdint.h>

#include "system_info.h"

#define SYSTEM_ARTWORK_FRAMEBUFFER_MAP_SIZE (32ULL * 1024ULL * 1024ULL)

typedef const uint8_t * ConstSystemArtworkPixelsPtr;
typedef uint32_t * SystemArtworkFramebufferPtr;

typedef struct SystemArtworkImage
{
    ConstSystemArtworkPixelsPtr pixels;
    uint32_t width;
    uint32_t height;
} SystemArtworkImage;

typedef const SystemArtworkImage * ConstSystemArtworkImagePtr;

static inline SystemArtworkImage SystemArtworkImage__new(ConstSystemArtworkPixelsPtr pixels, uint32_t width,
                                                         uint32_t height)
{
    return (SystemArtworkImage){.pixels = pixels, .width = width, .height = height};
}

Address SystemArtwork__framebuffer_kernel_address(Address mapped_base, FramebufferInfoPtr framebuffer);
uint64_t SystemArtwork__mapped_rows(FramebufferInfoPtr framebuffer, uint64_t mapped_bytes);
void SystemArtwork__draw_image(SystemArtworkFramebufferPtr pixels, FramebufferInfoPtr framebuffer,
                               ConstSystemArtworkImagePtr image);
void SystemArtwork__show_launch(SystemInfoPtr system_info);
void SystemArtwork__show_panic(SystemInfoPtr system_info, const char * message);
void SystemArtwork__show_panic_ascii(void);

#endif

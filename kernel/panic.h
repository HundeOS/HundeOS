#ifndef __KERNEL_HALT_H
#define __KERNEL_HALT_H

#include <stdbool.h>
#include <stdint.h>

#include "arch/panic.h"
#include "address.h"
#include "system_info.h"

#define PANIC_SCREEN_FRAMEBUFFER_MAP_SIZE (32ULL * 1024ULL * 1024ULL)

Address PanicScreen__framebuffer_kernel_address(Address mapped_base, FramebufferInfoPtr framebuffer);
uint64_t PanicScreen__mapped_rows(FramebufferInfoPtr framebuffer, uint64_t mapped_bytes);
bool PanicScreen__can_render_text(const char * text);

void Kernel__panic(const char * error, ProcessContextPtr ctx);

#endif

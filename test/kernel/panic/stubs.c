#include <stdarg.h>
#include <string.h>

#include "../../../kernel/arch/basic_io.h"
#include "../../../kernel/arch/cpu.h"
#include "../../../kernel/arch/panic.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/kernel_init.h"
#include "../../../kernel/print.h"

uint64_t __framebuffer_start[4096];
const char __hunde_logo_ascii[] = "logo";
const char __hunde_panic_ascii[] = "panic";
const uint8_t __hunde_logo_rgb_start[768 * 512 * 3] = {0xff, 0, 0};
const uint8_t __hunde_panic_rgb_start[768 * 512 * 3] = {0, 0xff, 0};
static char artwork_output[4096];

static Kernel panic_test_kernel = {
    .system_info = None(SystemInfo),
};

KernelPtr Kernel__instance()
{
    return &panic_test_kernel;
}

void BasicIO__puts(const char * str)
{
    strncat(artwork_output, str, sizeof(artwork_output) - strlen(artwork_output) - 1);
}

void ArtworkTest__reset(void)
{
    artwork_output[0] = '\0';
    memset(__framebuffer_start, 0, sizeof(__framebuffer_start));
    panic_test_kernel.system_info = None(SystemInfo);
}

const char * ArtworkTest__output(void)
{
    return artwork_output;
}

void Kernel__dump_state(ProcessContextPtr context)
{
}

void CPU__halt_all()
{
}

int k_printf(const char * format, ...)
{
    va_list args;
    va_start(args, format);
    va_end(args);

    return 0;
}

void k_println(const char * str)
{
}

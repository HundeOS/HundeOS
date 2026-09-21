#include <stdarg.h>

#include "../../../kernel/arch/basic_io.h"
#include "../../../kernel/arch/cpu.h"
#include "../../../kernel/arch/panic.h"
#include "../../../kernel/kernel_core.h"
#include "../../../kernel/kernel_init.h"
#include "../../../kernel/print.h"

const uint64_t __framebuffer_start[4096];

static Kernel panic_test_kernel = {
    .system_info = None(SystemInfo),
};

KernelPtr Kernel__instance()
{
    return &panic_test_kernel;
}

void BasicIO__puts(const char * str)
{
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

#include "panic.h"

#include "arch/basic_io.h"
#include "arch/cpu.h"
#include "artwork.h"
#include "kernel_core.h"
#include "kernel_init.h"
#include "print.h"

void Kernel__panic(const char * message, ProcessContextPtr context)
{
    KernelPtr kernel = Kernel__instance();
    match_optional(
        kernel->system_info,
        some(system_info)
        {
            SystemArtwork__show_panic(&system_info, message);
        },
        none()
        {
            SystemArtwork__show_panic_ascii();
        }
    );
    BasicIO__puts("Kernel panik: ");
    BasicIO__puts(message);
    BasicIO__puts("\nSystem halted\n");
    k_printf("Kernel panik: %s\n", message);
    k_println("System halted");

    Kernel__dump_state(context);
    CPU__halt_all();
}

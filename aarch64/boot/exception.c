#include "exceptions.h"
#include "print.h"
#include "boot_strings.h"

__BOOT__(void) boot_panic(const char * msg) {
    _print(msg);
    while (true) {
        __asm__ volatile("wfe");
    }
}

__BOOT__(void) boot_vectors_init(void) {
    extern void *boot_vector_table;

    write_vbar_el1((uint64_t)&boot_vector_table);
}

__BOOT__(void) boot_handler(boot_trapframe_t * trap)
{
    _print(fault);
    uint64_t esr = rd_esr_el1();
    uint64_t elr = rd_elr_el1();
    uint64_t far = rd_far_el1();
    uint64_t spsr = rd_spsr_el1();
    uint64_t tcr = rd_tcr();

    const uint64_t iss = esr & 0x01FFFFFFu;
    const unsigned fsc = iss & 0x3F;

    uint64_t ec = (esr >> 26) & 0x3F;
    unsigned il = (esr >> 25) & 1;

    _print(EC);
    _print_unsigned(ec);
    _print(ESR);
    _print_unsigned(esr);
    _print(ELR);
    _print_unsigned(elr);
    _print(FAR);
    _print_unsigned(far);
    _print(IL);
    _print_unsigned(il);
    _print(FSC);
    _print_unsigned(fsc);
    _print(TCR);
    _print_unsigned(tcr);
    while (1) {
        __asm__ volatile("wfe");
    }
}



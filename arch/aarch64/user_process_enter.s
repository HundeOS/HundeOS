.global _user_process_enter
_user_process_enter:
    // x0=arggc, x1=argv, x2=spsr, x3=sp, x4=page_table_phys, x5=entry_point
    msr sp_el0, x3
    dsb ish
    msr ttbr0_el1, x4
    msr spsr_el1, x2
    isb
    // If you just changed to a different ASID/root, do a full shootdown.
    tlbi vmalle1
    dsb ish
    isb

    blr x5                // x0=argc, x1=argv if you want to pass them
    // exit(status in x0)
    mov w8, #0              // svc number: 0 = sys_exit (you pick)
    svc #0
    b .                     // if kernel returns (it shouldn’t), spin
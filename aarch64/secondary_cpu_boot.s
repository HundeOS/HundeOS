// secondary_entry.S
    .section .text.secondary, "ax"
    .align  7
    .global secondary_psci_entry
secondary_psci_entry:
    // x0 = context_id (we pass SP)
    mov     sp, x0
    mrs     x1, mpidr_el1
    and     x1, x1, #0xFF     // cpu_id = Aff0
    bl      start_cpu
1:  wfe
    b       1b
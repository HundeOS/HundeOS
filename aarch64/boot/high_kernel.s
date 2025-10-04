// aarch64/high_entry.S
    .section .text, "ax"
    .p2align 4
    .global enter_high_kernel2
enter_high_kernel2:
    bl      print_crumb

    mov     sp, x23        // stack setup

    bl      print_crumb

    msr     vbar_el1, x24   // kernel vectors setup
    isb

    bl      print_crumb

    br      x25              // branch to high VA entrypoint
halt:
    wfe
    b       halt

print_crumb:
    mov     w3, #'.'
    mov     x10, #0x09000000       // PL011 base (virt==phys for low map)
    strb    w3, [x10]              // best-effort breadcrumb
    ret

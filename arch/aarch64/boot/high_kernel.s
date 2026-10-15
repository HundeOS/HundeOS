// aarch64/high_entry.S
    .section .text, "ax"
    .p2align 4
    .global enter_high_kernel
enter_high_kernel:
    mov     w0, #'3'
    bl      _send

    mov     sp, x23        // stack setup

    //bl      print_crumb

    mov     w0, #'4'
    bl      _send

    mov     x0, x26
    br      x25              // branch to high VA entrypoint
halt:
    wfe
    b       halt

print_crumb:
    mov     w3, #'.'
    mov     x10, #0x09000000       // PL011 base (virt==phys for low map)
    strb    w3, [x10]              // best-effort breadcrumb
    ret

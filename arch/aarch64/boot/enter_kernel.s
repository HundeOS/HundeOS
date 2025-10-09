// aarch64/boot_tramp.S
    .section .boot.text, "ax"
    .p2align 4
    .global enter_kernel
enter_kernel:
    // stack pointer is in x0
    // vector table pointer is in x1
    // function is in x2

    //bl      print_crumb                  // print '.'

    // Put pointer in a callee-saved register so the next code can use it
    mov     x23, x0     // stack (absolute VA)
    mov     x24, x1     // vector_table  (absolute VA)
    mov     x25, x2    // entrypoint  (absolute VA)

    //bl      print_crumb

    mov     x19, x3

    adr     x3, .Lpool            // PC-relative address of pool
    ldr     x0, [x3]              // x0 = &enter_high_kernel2 (high VA)

    //bl      print_crumb

    br      x0                    // tail-jump; no return

    .p2align 3
print_crumb:
    mov     w3, #'.'
    mov     x10, #0x09000000       // PL011 base (virt==phys for low map)
    strb    w3, [x10]              // best-effort breadcrumb
    ret

.Lpool:
    .quad   enter_high_kernel     // linker fills the *high VA*

.include "arch/aarch64/asm_macros.inc"

.section .user.text, "ax"
.align  7
.global __el0_entry
__el0_entry:
    // actual program addr is in x8
    blr x8
    // x0 has the exit/return code
    mov x8, #0
    svc #2
    b .

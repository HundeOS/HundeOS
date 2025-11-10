// ================================
//  UART Debug Macros (PL011)
// ================================

// Adjust this to your own UART base
.equ UART0_BASE,     0xFFFF000000000000  // <-- change if needed
.equ UART0_DR,       (UART0_BASE + 0x00)
.equ UART0_FR,       (UART0_BASE + 0x18)
.equ UARTFR_TXFF,    (1 << 5)

// --------------------------------
// uart_putc wX
// Sends a character in wX
// --------------------------------
.macro uart_putc, reg
1:
    ldr x1, =UART0_FR
    ldr w2, [x1]
    tst w2, #UARTFR_TXFF
    b.ne 1b                    // wait while TX FIFO full
    ldr x1, =UART0_DR
    strb \reg, [x1]            // store low byte of wX
.endm

// --------------------------------
// uart_puts label
// Prints a null-terminated string
// --------------------------------
.macro uart_puts, label
    adr x0, \label
1:
    ldrb w1, [x0], #1
    cbz w1, 2f
    uart_putc w1
    b 1b
2:
.endm

// --------------------------------
// uart_print_hex xN
// Prints a 64-bit register in hex
// --------------------------------
// uart_print_hex reg -- print 64-bit register as 16 hex digits
// Requires: uart_putc (takes w0 = char)
// Clobbers: x10–x15
// uart_print_hex reg  -- print 64-bit value as 16 hex digits (MSB first)
// Requires: uart_putc_imm or uart_putc (w0 = char)
// Clobbers: x10–x14
.macro uart_print_hex, reg
    mov     x10, \reg        // working value
    mov     x11, #60         // counts down bits shifted

100:
    mov     x12, x10
    lsl     x12, x12, #0     // ensure copy (avoids alias)
    and     x12, x12, #(0xF << 60)   // isolate top nibble
    lsr     x12, x12, #60            // move it down to bottom bits
    and     x12, x12, #0xF            // ensure its just one nibble

    cmp     x12, #10                // in A-F ?
    b.lo    200f
    sub     x12, x12, #10
    add     x12, x12, #'A'     // convert val to hex char
    b       300f                      // go print
200:
    add     x12, x12, #'0'
300:
    mov     w0, w12          // get the byte val
    uart_putc w0         // print
    lsl     x10, x10, #4     // bring next nibble to top
    subs    x11, x11, #4     // count down by a nibble
    b.ge    100b               // keep going if there are bits left
.endm

// --------------------------------
// uart_print_reg "Label", xN
// Prints a label, tab, hex value, newline
// --------------------------------
.macro uart_print_reg, name, reg
    .ascii "\name=0x"
    .byte 0
1:
    adr     x15, 1b
    ldrb    w14, [x15], #1
    cbz     w14, 2f
    uart_putc w14
    b       1b
2:
    uart_print_hex \reg
    uart_putc_imm #'\r'
    uart_putc_imm #'\n'
.endm

.macro uart_putc_imm, imm
    mov w0, \imm
    uart_putc w0
.endm


.macro save_context
    // Reserve full ProcessContext size (0x520)
    sub sp, sp, #0x320

    // --- General-purpose registers (x0–x30) ---
    stp x0,  x1,  [sp, #16 * 0]
    stp x2,  x3,  [sp, #16 * 1]
    stp x4,  x5,  [sp, #16 * 2]
    stp x6,  x7,  [sp, #16 * 3]
    stp x8,  x9,  [sp, #16 * 4]
    stp x10, x11, [sp, #16 * 5]
    stp x12, x13, [sp, #16 * 6]
    stp x14, x15, [sp, #16 * 7]
    stp x16, x17, [sp, #16 * 8]
    stp x18, x19, [sp, #16 * 9]
    stp x20, x21, [sp, #16 * 10]
    stp x22, x23, [sp, #16 * 11]
    stp x24, x25, [sp, #16 * 12]
    stp x26, x27, [sp, #16 * 13]
    stp x28, x29, [sp, #16 * 14]
    str x30,      [sp, #16 * 15]      // 0xF0

    // --- Save SP, ELR, SPSR ---
    mrs x0, sp_el0
    str x0, [sp, #0xF8]
    mrs x0, elr_el1
    str x0, [sp, #0x100]
    mrs x0, spsr_el1
    str x0, [sp, #0x108]

    // --- SIMD/FPU registers (q0–q31) ---
    add x1, sp, #0x110
    stp q0,  q1,  [x1, #16 * 0]
    stp q2,  q3,  [x1, #16 * 1]
    stp q4,  q5,  [x1, #16 * 2]
    stp q6,  q7,  [x1, #16 * 3]
    stp q8,  q9,  [x1, #16 * 4]
    stp q10, q11, [x1, #16 * 5]
    stp q12, q13, [x1, #16 * 6]
    stp q14, q15, [x1, #16 * 7]
    stp q16, q17, [x1, #16 * 8]
    stp q18, q19, [x1, #16 * 9]
    stp q20, q21, [x1, #16 * 10]
    stp q22, q23, [x1, #16 * 11]
    stp q24, q25, [x1, #16 * 12]
    stp q26, q27, [x1, #16 * 13]
    stp q28, q29, [x1, #16 * 14]
    stp q30, q31, [x1, #16 * 15]

    // --- Save FPCR/FPSR ---
    mrs x2, fpcr
    mrs x3, fpsr
    str x2, [sp, #0x310]
    str x3, [sp, #0x318]
.endm


.macro restore_context
    // --- Restore SIMD/FPU registers ---
    mov x30, sp
    add x1, x30, #0x110 // q[n] offset

    ldp q0,  q1,  [x1, #16 * 0]
    ldp q2,  q3,  [x1, #16 * 1]
    ldp q4,  q5,  [x1, #16 * 2]
    ldp q6,  q7,  [x1, #16 * 3]
    ldp q8,  q9,  [x1, #16 * 4]
    ldp q10, q11, [x1, #16 * 5]
    ldp q12, q13, [x1, #16 * 6]
    ldp q14, q15, [x1, #16 * 7]
    ldp q16, q17, [x1, #16 * 8]
    ldp q18, q19, [x1, #16 * 9]
    ldp q20, q21, [x1, #16 * 10]
    ldp q22, q23, [x1, #16 * 11]
    ldp q24, q25, [x1, #16 * 12]
    ldp q26, q27, [x1, #16 * 13]
    ldp q28, q29, [x1, #16 * 14]
    ldp q30, q31, [x1, #16 * 15]

    //--- Restore FPCR/FPSR ---
    ldr x2, [x30, #0x310]  //fpcr offset
    ldr x3, [x30, #0x318]  //fpsr offset

    msr fpcr, x2
    msr fpsr, x3

    // --- Restore SP, ELR, SPSR ---
    ldr x1, [x30, #0xF8]
    msr sp_el0, x1

    ldr x1, [x30, #0x100]
    msr elr_el1, x1

    ldr x1, [x30, #0x108]
    msr spsr_el1, x1

    mov x1, x30
    // --- Restore general-purpose registers ---
    ldr x30,      [x1, #16 * 15]
    ldp x28, x29, [x1, #16 * 14]
    ldp x26, x27, [x1, #16 * 13]
    ldp x24, x25, [x1, #16 * 12]
    ldp x22, x23, [x1, #16 * 11]
    ldp x20, x21, [x1, #16 * 10]
    ldp x18, x19, [x1, #16 * 9]
    ldp x16, x17, [x1, #16 * 8]
    ldp x14, x15, [x1, #16 * 7]
    ldp x12, x13, [x1, #16 * 6]
    ldp x10, x11, [x1, #16 * 5]
    ldp x8,  x9,  [x1, #16 * 4]
    ldp x6,  x7,  [x1, #16 * 3]
    ldp x4,  x5,  [x1, #16 * 2]
    ldp x2,  x3,  [x1, #16 * 1]
    ldp x0,  x1,  [x1, #16 * 0]

    add sp, sp, #0x320
.endm

.macro restore_parked_context
    // --- Restore SIMD/FPU registers ---
    mov x30, x0
    add x1, x30, #0x110 // q[n] offset

    ldp q0,  q1,  [x1, #16 * 0]
    ldp q2,  q3,  [x1, #16 * 1]
    ldp q4,  q5,  [x1, #16 * 2]
    ldp q6,  q7,  [x1, #16 * 3]
    ldp q8,  q9,  [x1, #16 * 4]
    ldp q10, q11, [x1, #16 * 5]
    ldp q12, q13, [x1, #16 * 6]
    ldp q14, q15, [x1, #16 * 7]
    ldp q16, q17, [x1, #16 * 8]
    ldp q18, q19, [x1, #16 * 9]
    ldp q20, q21, [x1, #16 * 10]
    ldp q22, q23, [x1, #16 * 11]
    ldp q24, q25, [x1, #16 * 12]
    ldp q26, q27, [x1, #16 * 13]
    ldp q28, q29, [x1, #16 * 14]
    ldp q30, q31, [x1, #16 * 15]

    //--- Restore FPCR/FPSR ---
    ldr x2, [x30, #0x310]  //fpcr offset
    ldr x3, [x30, #0x318]  //fpsr offset

    msr fpcr, x2
    msr fpsr, x3

    // --- Restore SP, ELR, SPSR ---
    ldr x1, [x30, #0xF8]
    msr sp_el0, x1

    ldr x1, [x30, #0x100]
    msr elr_el1, x1

    ldr x1, [x30, #0x108]

    ldr x1, [x30, #0x108]
    msr spsr_el1, x1

    mov x1, x30
    // --- Restore general-purpose registers ---
    ldr x30,      [x1, #16 * 15]
    ldp x28, x29, [x1, #16 * 14]
    ldp x26, x27, [x1, #16 * 13]
    ldp x24, x25, [x1, #16 * 12]
    ldp x22, x23, [x1, #16 * 11]
    ldp x20, x21, [x1, #16 * 10]
    ldp x18, x19, [x1, #16 * 9]
    ldp x16, x17, [x1, #16 * 8]
    ldp x14, x15, [x1, #16 * 7]
    ldp x12, x13, [x1, #16 * 6]
    ldp x10, x11, [x1, #16 * 5]
    ldp x8,  x9,  [x1, #16 * 4]
    ldp x6,  x7,  [x1, #16 * 3]
    ldp x4,  x5,  [x1, #16 * 2]
    ldp x2,  x3,  [x1, #16 * 1]
    ldp x0,  x1,  [x1, #16 * 0]
.endm

.macro unwind_kernel_stack
    // this is the current kernel sp set via the interrupt
    // unwind
    add sp, sp, #0x320
.endm

.macro restore_context_preserve_x0
    // --- Restore SIMD/FPU registers ---
    mov x30, sp
    add x1, x30, #0x110 // q[n] offset

    ldp q0,  q1,  [x1, #16 * 0]
    ldp q2,  q3,  [x1, #16 * 1]
    ldp q4,  q5,  [x1, #16 * 2]
    ldp q6,  q7,  [x1, #16 * 3]
    ldp q8,  q9,  [x1, #16 * 4]
    ldp q10, q11, [x1, #16 * 5]
    ldp q12, q13, [x1, #16 * 6]
    ldp q14, q15, [x1, #16 * 7]
    ldp q16, q17, [x1, #16 * 8]
    ldp q18, q19, [x1, #16 * 9]
    ldp q20, q21, [x1, #16 * 10]
    ldp q22, q23, [x1, #16 * 11]
    ldp q24, q25, [x1, #16 * 12]
    ldp q26, q27, [x1, #16 * 13]
    ldp q28, q29, [x1, #16 * 14]
    ldp q30, q31, [x1, #16 * 15]

    //--- Restore FPCR/FPSR ---
    ldr x2, [x30, #0x310]  //fpcr offset
    ldr x3, [x30, #0x318]  //fpsr offset

    msr fpcr, x2
    msr fpsr, x3

    // --- Restore SP, ELR, SPSR ---
    ldr x1, [x30, #0xF8]
    msr sp_el0, x1

    ldr x1, [x30, #0x100]
    msr elr_el1, x1

    ldr x1, [x30, #0x108]
    msr spsr_el1, x1

    mov x1, x30
    // --- Restore general-purpose registers ---
    ldr x30,      [x1, #16 * 15]
    ldp x28, x29, [x1, #16 * 14]
    ldp x26, x27, [x1, #16 * 13]
    ldp x24, x25, [x1, #16 * 12]
    ldp x22, x23, [x1, #16 * 11]
    ldp x20, x21, [x1, #16 * 10]
    ldp x18, x19, [x1, #16 * 9]
    ldp x16, x17, [x1, #16 * 8]
    ldp x14, x15, [x1, #16 * 7]
    ldp x12, x13, [x1, #16 * 6]
    ldp x10, x11, [x1, #16 * 5]
    ldp x8,  x9,  [x1, #16 * 4]
    ldp x6,  x7,  [x1, #16 * 3]
    ldp x4,  x5,  [x1, #16 * 2]
    ldp x2,  x3,  [x1, #16 * 1]
    ldr x1,  [x1, #8]       // restore only x1
    // NOTE: x0 intentionally not restored (used for syscall return)

    add sp, sp, #0x320
.endm
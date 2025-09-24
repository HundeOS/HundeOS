/* linker.ld — Raspberry Pi 3/4 (AArch64) style, image at 0x80000
 *
 * Layout:
 *   0x80000: .text.boot (contains _start)
 *            .text.vectors (2 KiB aligned; KEEP)
 *            .text / .rodata
 *            .data
 *            .bss (NOLOAD)  -> __bss_start/__bss_end/__bss_size
 *            .bss.stack (NOLOAD; KEEP) -> EL3/EL2/EL1 stacks
 */

ENTRY(_start)

SECTIONS
{
    /* virt bootload puts kernel here */
    . = 0x40080000;

    /* Early boot (reset path) */
    .text.boot : ALIGN(64) {
        KEEP(*(.text.boot))
    }

    /* Exception vectors: must be 2 KiB aligned and kept */
    . = ALIGN(2048);
    .text.vectors : {
        KEEP(*(.text.vectors))
    }

    /* Normal code, then read-only data */
    .text : ALIGN(64) {
        *(.text .text.* .gnu.linkonce.t*)
    }

    .rodata : ALIGN(64) {
        *(.rodata .rodata.* .gnu.linkonce.r*)
    }

    /* Writable data */
    PROVIDE(_data = .);
    .data : ALIGN(64) {
        *(.data .data.* .gnu.linkonce.d*)
        *(.init_array .init_array.*)
        *(.fini_array .fini_array.*)
        *(.got .got.*)
    }

    /* Zero-init (not stored in the image) */
    .bss (NOLOAD) : ALIGN(16) {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
        __bss_size = (__bss_end - __bss_start) >> 3;  /* words = 8-byte stores */
    }

    /* Stacks for EL3/EL2/EL1 (from boot.S): keep but don’t bloat file */
    .bss.stack (NOLOAD) : ALIGN(16) {
        KEEP(*(.bss.stack))
    }

    _end = .;

    /* Toss toolchain noise */
    /DISCARD/ : {
        *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*)
    }
}

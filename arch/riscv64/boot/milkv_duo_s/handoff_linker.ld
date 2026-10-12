OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x8d000000;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(8)
    {
        *(.rodata .rodata.*)
    }

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
    }
}

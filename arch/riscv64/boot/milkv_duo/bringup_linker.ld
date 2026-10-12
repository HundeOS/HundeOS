OUTPUT_ARCH(riscv)
ENTRY(_start)

SECTIONS
{
    . = 0x80200000;
    __image_start = .;

    .text : ALIGN(4K)
    {
        KEEP(*(.text.entry))
        *(.text .text.*)
    }

    .rodata : ALIGN(4K)
    {
        *(.rodata .rodata.*)
    }

    .data : ALIGN(4K)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
    }

    .bss (NOLOAD) : ALIGN(4K)
    {
        __bss_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __bss_end = .;
    }

    .boot.stack (NOLOAD) : ALIGN(4K)
    {
        . += 64K;
        __boot_stack_top = .;
    }

    __image_end = .;
    ASSERT(__image_end <= 0x80300000, "Milk-V Duo bring-up payload exceeds its 1 MiB reservation")

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
    }
}

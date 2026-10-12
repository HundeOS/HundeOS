OUTPUT_ARCH(riscv)
ENTRY(_start)

PHDRS
{
    boot_text PT_LOAD FLAGS(5);
    boot_data PT_LOAD FLAGS(6);
    kernel_text PT_LOAD FLAGS(5);
    kernel_data PT_LOAD FLAGS(6);
}

RISCV_HIGHER_HALF_OFFSET = 0xffffffc000000000;
RISCV_PHYSICAL_LOAD_BASE = 0x80200000;
RISCV_USABLE_MEMORY_END = 0x83f40000;

SECTIONS
{
    . = RISCV_PHYSICAL_LOAD_BASE;
    __image_physical_start = .;

    .boot.text : ALIGN(4K)
    {
        KEEP(*(.text.entry))
        *(.boot.text .boot.text.*)
    } :boot_text

    .boot.bss (NOLOAD) : ALIGN(4K)
    {
        __boot_bss_start = .;
        *(.boot.bss .boot.bss.*)
        . = ALIGN(8);
        __boot_bss_end = .;
    } :boot_data

    .boot.stack (NOLOAD) : ALIGN(4K)
    {
        KEEP(*(.boot.stack))
    } :boot_data

    __boot_physical_end = ALIGN(., 4K);
    __physical_cursor = __boot_physical_end;

    . = __physical_cursor + RISCV_HIGHER_HALF_OFFSET;
    __kernel_virtual_start = .;

    .text : AT(__physical_cursor) ALIGN(4K)
    {
        *(.text .text.*)
    } :kernel_text
    __physical_cursor = ALIGN(LOADADDR(.text) + SIZEOF(.text), 4K);

    . = __physical_cursor + RISCV_HIGHER_HALF_OFFSET;
    .rodata : AT(__physical_cursor) ALIGN(4K)
    {
        *(.rodata .rodata.*)
    } :kernel_text
    __physical_cursor = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), 4K);

    . = __physical_cursor + RISCV_HIGHER_HALF_OFFSET;
    .data : AT(__physical_cursor) ALIGN(4K)
    {
        *(.data .data.*)
        *(.sdata .sdata.*)
    } :kernel_data
    __physical_cursor = ALIGN(LOADADDR(.data) + SIZEOF(.data), 4K);

    . = __physical_cursor + RISCV_HIGHER_HALF_OFFSET;
    .binaries : AT(__physical_cursor) ALIGN(4K)
    {
        __embedded_binaries_start = .;
        KEEP(*(.binaries))
        . = ALIGN(4K);
        __embedded_binaries_end = .;
    } :kernel_data
    __embedded_binaries_phys_start = LOADADDR(.binaries);
    __embedded_binaries_phys_end = LOADADDR(.binaries) + SIZEOF(.binaries);
    __physical_cursor = ALIGN(__embedded_binaries_phys_end, 4K);

    . = __physical_cursor + RISCV_HIGHER_HALF_OFFSET;
    .bss (NOLOAD) : AT(__physical_cursor) ALIGN(4K)
    {
        __kernel_bss_virtual_start = .;
        *(.bss .bss.*)
        *(.sbss .sbss.*)
        *(COMMON)
        . = ALIGN(8);
        __kernel_bss_virtual_end = .;
    } :kernel_data

    __kernel_bss_physical_start = LOADADDR(.bss);
    __kernel_bss_physical_end = LOADADDR(.bss) + SIZEOF(.bss);
    __image_physical_end = ALIGN(__kernel_bss_physical_end, 4K);
    __image_virtual_start = __image_physical_start + RISCV_HIGHER_HALF_OFFSET;
    __image_virtual_end = __image_physical_end + RISCV_HIGHER_HALF_OFFSET;
    __boot_stack_virtual_start = __boot_stack_start + RISCV_HIGHER_HALF_OFFSET;
    __boot_stack_virtual_top = __boot_stack_top + RISCV_HIGHER_HALF_OFFSET;
    __riscv_boot_main_physical = RiscVBoot__main - RISCV_HIGHER_HALF_OFFSET;
    __riscv_boot_secondary_physical = RiscVBoot__secondary - RISCV_HIGHER_HALF_OFFSET;
    __riscv_secondary_entry_virtual = RiscVSecondary__entry + RISCV_HIGHER_HALF_OFFSET;

    ASSERT(__image_physical_end <= RISCV_USABLE_MEMORY_END, "Milk-V Duo kernel exceeds usable RAM")

    /DISCARD/ :
    {
        *(.comment)
        *(.note .note.*)
        *(.eh_frame .eh_frame.*)
    }
}

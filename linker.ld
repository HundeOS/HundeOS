/* kernel.ld — AArch64, 4 KiB pages, image @ 0x40080000
   Simple: 2 PT_LOAD segments (RX and RW), 4 KiB alignment throughout. */

ENTRY(_start)

PHDRS {
  boot  PT_LOAD FLAGS(5);            /* R X: low boot code (VMA=LMA low)   */
  bootdata PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  ktext PT_LOAD FLAGS(5);            /* R X: high .text/.rodata (VMA high, LMA low via AT) */
  kdata PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  kio PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
}

KERNEL_HIGH_BASE = 0xFFFF000000000000;
KERNEL_TABLE_BASE = 0xFFFF800000000000;
KERNEL_HEAP_BASE = 0xFFFFF00000000000;
KERNEL_BASE = 0x40080000;

HEAP_END = 2 * 1024 * 1024 * 1024; /* 2 GB */

KERNEL_HIGH_PHYS = 0x80000000;
BLOCK_2MB = 0x200000;
PAGE_4KB = 0x1000;
PAGE_2KB = 0x800;
SECTIONS
{
  . = KERNEL_BASE;

  __kernel_start = .;

  /* ----- TEXT & RODATA (RX) ----- */
  . = ALIGN(PAGE_4KB);

  __boot_start = .;
  .boot.text : ALIGN(PAGE_4KB) {
    KEEP(*(.boot))
    KEEP(*(.boot.text))
  } :boot

  . = ALIGN(PAGE_4KB);
  .boot.vectors : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.vectors))
  }: boot

  .boot.data : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.data))
    KEEP(*(.boot.data.*))
  } :bootdata


  .boot.data.table : ALIGN(2M) {
    __table_phys_start = .;
    KEEP(*(.boot.data.table))
    KEEP(*(.boot.data.table.*))
  } :bootdata

  __table_phys_end = .;


  .boot.rodata : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.rodata))
    KEEP(*(.boot.rodata.*))
  } :bootdata

  /* Stacks (no load) */
  . = ALIGN(PAGE_4KB);
  __boot_bss_start = .;
  .boot.bss.stack (NOLOAD) : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.bss.stack))
  } :bootdata
  __boot_bss_end = .;
  __boot_end = ALIGN(__boot_bss_end, PAGE_4KB);


   /** MMIO first **/
  /* Hi memory kernel */
  . = KERNEL_HIGH_BASE;
  . = ALIGN(PAGE_4KB);
    .bss.mmio (NOLOAD):
    {
      KEEP(*(.bss.mmio))
    } : kio
    __mmio_start = .;
    . += 2M;
    __mmio_end = .;
    . += 2M;

    . = ALIGN(BLOCK_2MB);
    .bss.gicd (NOLOAD):
    {
      KEEP(*(.bss.gicd))
    } : kio
    __gicd_start = .;
    . += 2M;
    __gicd_end = .;

  __lma_cursor = ALIGN(LOADADDR(.boot.rodata) + SIZEOF(.boot.rodata), BLOCK_2MB);
  __lma_start = __lma_cursor;
  __high_phys_base = __lma_cursor;

  .text : AT(ALIGN(__lma_cursor, BLOCK_2MB)) ALIGN(PAGE_4KB) {
    *(.text .text.*)
  } :ktext
  __text_virt = . - SIZEOF(.text);
  __text_phys = LOADADDR(.text);

  __lma_cursor = ALIGN(LOADADDR(.text) + SIZEOF(.text), PAGE_4KB);

  .text.vectors : AT(ALIGN(__lma_cursor, PAGE_2KB)) ALIGN(PAGE_2KB) {
    KEEP(*(.text.vectors))
  } :ktext
  __vectors_pa = LOADADDR(.text.vectors);
  __lma_cursor = ALIGN(LOADADDR(.text.vectors) + SIZEOF(.text.vectors), PAGE_4KB);

  .rodata : AT(ALIGN(__lma_cursor, PAGE_4KB)) ALIGN(PAGE_4KB) {
    *(.rodata .rodata.* .gnu.linkonce.r*)
  } :ktext

  __lma_cursor = ALIGN(LOADADDR(.rodata) + SIZEOF(.rodata), PAGE_4KB);
  /* ----- DATA (RW) ----- */

  .data : AT(ALIGN(__lma_cursor, PAGE_4KB)) ALIGN(PAGE_4KB)
  {
    *(.data .data.* .gnu.linkonce.d*)
    *(.init_array .init_array.*)
    *(.fini_array .fini_array.*)
    *(.got .got.*)
  } :kdata
  __lma_cursor = ALIGN(LOADADDR(.data) + SIZEOF(.data), PAGE_4KB);

/* ----- BSS (RW, no load) ----- */
  . = ALIGN(PAGE_4KB);
  .bss (NOLOAD):
  {
    *(.bss .bss.*)
    *(COMMON)
  } :kdata
  __lma_cursor = ALIGN(__lma_cursor + SIZEOF(.data), PAGE_4KB);

   /* reset of system memory*/
  .bss.stack (NOLOAD):
  {
    KEEP(*(.bss.stack))
  } :kdata

  .binaries : ALIGN(BLOCK_2MB)
  {
    KEEP(*(.binaries))
  } :kdata

  .kstack (NOLOAD) : ALIGN(PAGE_4KB) {
      __k_stack_start = .;
      . += 2M;                 /* pick a size */
      __k_stack_end   = .;
      __k_stack_high  = __k_stack_end;  /* top of stack grows down */
  } :kdata

  __lma_cursor = ALIGN(__lma_cursor + SIZEOF(.kstack), PAGE_4KB);
  . += 4M;
  __lma_kernel_end = __text_phys + . - KERNEL_HIGH_BASE;
  __lma_end = __lma_kernel_end;

  . = KERNEL_TABLE_BASE;
  __table_virt_start = ALIGN(BLOCK_2MB);
  . += 2M;
  __table_virt_end = .;

  . = KERNEL_HEAP_BASE;
  __lma_cursor = ALIGN(__lma_cursor + 4M, BLOCK_2MB);

  .bss.heap : AT(ALIGN(__lma_cursor, BLOCK_2MB)) ALIGN(BLOCK_2MB)
  {
    __heap_start = .;
    KEEP(*(.bss.heap))
  } : kdata
  __heap_start_phys = LOADADDR(.bss.heap);
  __lma_heap_bootstrap_end = __heap_start_phys + . - KERNEL_HEAP_BASE;
  . += HEAP_END;
  __heap_end = .;
  __heap_end_phys = __heap_start_phys + HEAP_END;
  /DISCARD/ : {
    *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*)
  }
}

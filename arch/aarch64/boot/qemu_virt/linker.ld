/* kernel.ld — AArch64, 4 KiB pages, image @ 0x40080000
   Simple: 2 PT_LOAD segments (RX and RW), 4 KiB alignment throughout. */

ENTRY(_start)

PHDRS {
  boot  PT_LOAD FLAGS(5);            /* R X: low boot code (VMA=LMA low)   */
  bootdata PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  bootalloc PT_LOAD FLAGS(6);
  ktext PT_LOAD FLAGS(5);            /* R X: high .text/.rodata (VMA high, LMA low via AT) */
  kdata PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  kstack PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  kbinaries PT_LOAD FLAGS(6);       /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  utext PT_LOAD FLAGS(6);       /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  kheap PT_LOAD FLAGS(6);       /* R W: high .data/.bss (VMA high, LMA low via AT)   */
  kio PT_LOAD FLAGS(6);            /* R W: high .data/.bss (VMA high, LMA low via AT)   */
}

KERNEL_HIGH_BASE = 0xFFFF000000000000;
KERNEL_BIN_BASE = 0xFFFFA00000000000;
KERNEL_STACK_BASE = 0xFFFF800000000000;
KERNEL_TABLE_BASE = 0xFFFFC00000000000;
KERNEL_HEAP_BASE = 0xFFFFF00000000000;
KERNEL_BASE = DEFINED(KERNEL_BASE) ? KERNEL_BASE : 0x40200000;
/*KERNEL_BASE = 0x00;*/
BOOT_ALLOCATION_BASE = DEFINED(BOOT_ALLOCATION_BASE) ? BOOT_ALLOCATION_BASE : 0x50000000;
KERNEL_STACK_SIZE = DEFINED(KERNEL_STACK_SIZE) ? KERNEL_STACK_SIZE : 200M;

HEAP_END = 2 * 1024 * 1024 * 1024; /* 2 GB */

KERNEL_STACK_PHYS = 0xC0000000;
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
  } :bootdata


  .boot.data.table : ALIGN(2M) {
    __table_phys_start = .;
    KEEP(*(.boot.data.table))
    KEEP(*(.boot.data.table.*))
    __table_phys_end = .;
  } :bootdata

  .boot.data.extra : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.data.*))
  } :bootdata


  .boot.rodata : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.rodata))
    KEEP(*(.boot.rodata.*))
  } :bootdata

  /* Stacks (no load) */
  . = ALIGN(PAGE_4KB);
  __boot_bss_start = .;
  .boot.bss.stack (NOLOAD) : ALIGN(PAGE_4KB) {
    KEEP(*(.boot.bss.stack))
    KEEP(*(.boot.bss))
    KEEP(*(.boot.bss.*))
  } :bootdata
  __boot_bss_end = .;
  __boot_loaded_end = ALIGN(__boot_bss_end, PAGE_4KB);

  .boot.allocation BOOT_ALLOCATION_BASE (NOLOAD) : ALIGN(PAGE_4KB) {
    __boot_allocation_start = .;
    KEEP(*(.boot.allocation))
    KEEP(*(.boot.allocation.*))
    __boot_allocation_end = .;
  } :bootalloc
  __boot_end = ALIGN(ADDR(.boot.allocation) + SIZEOF(.boot.allocation), PAGE_4KB);


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

    . = ALIGN(BLOCK_2MB);
    .bss.framebuffer (NOLOAD):
    {
      KEEP(*(.bss.framebuffer))
    } : kio
    __framebuffer_start = .;
    . += 32M;
    __framebuffer_end = .;

  __lma_cursor = ALIGN(LOADADDR(.boot.rodata) + SIZEOF(.boot.rodata), BLOCK_2MB);
  __lma_start = __lma_cursor;
  __high_phys_base = __lma_cursor;

  /** Start of kernel code and data **/
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
  __lma_kernel_end = __text_phys + . - KERNEL_HIGH_BASE;
  __lma_end = __lma_kernel_end;

   /* reset of system memory*/
  .bss.stack (NOLOAD):
  {
    KEEP(*(.bss.stack))
  } :kdata


  __lma_cursor = ALIGN(__lma_cursor + SIZEOF(.bss.stack), PAGE_4KB);
  . = KERNEL_BIN_BASE;

  .binaries : AT(ALIGN(__lma_cursor, BLOCK_2MB)) ALIGN(BLOCK_2MB)
  {
    __embedded_binaries_start = .;

    KEEP(*(.binaries))
    . = ALIGN(PAGE_4KB);
  } :kbinaries
  __embedded_binaries_end = .;
  __embedded_binaries_phys_start = LOADADDR(.binaries);
  __embedded_binaries_phys_end = LOADADDR(.binaries) + SIZEOF(.binaries);
  __lma_cursor = ALIGN(LOADADDR(.binaries) + SIZEOF(.binaries), PAGE_4KB);

  .user.text : AT(ALIGN(__lma_cursor, PAGE_4KB))
  {
    __user_jump_start = .;
    KEEP(*(.user.text));
  } :utext
  __user_jump_end = .;
  __user_jump_phys_start = LOADADDR(.user.text);
  __user_jump_phys_end = LOADADDR(.user.text) + SIZEOF(.user.text);

  __lma_cursor = LOADADDR(.user.text) + SIZEOF(.user.text);
  ASSERT(__lma_cursor <= __boot_allocation_start, "kernel image overlaps boot allocations")
  __lma_cursor = __boot_end;


  . = KERNEL_STACK_BASE;
  .kstack :AT(ALIGN(__lma_cursor, BLOCK_2MB)) ALIGN(BLOCK_2MB) {
      __k_stack_start = .;
      . += KERNEL_STACK_SIZE;
      __k_stack_end   = .;
      __k_stack_high  = __k_stack_end;  /* top of stack grows down */
  } :kstack
  __k_stack_phys_start = LOADADDR(.kstack);
  __k_stack_phys_end = LOADADDR(.kstack) + SIZEOF(.kstack);

  __lma_cursor = ALIGN(__lma_cursor + SIZEOF(.kstack), PAGE_4KB);
  . += 4M;

  . = KERNEL_TABLE_BASE;
  __table_virt_start = ALIGN(BLOCK_2MB);
  . += ALIGN(__table_phys_end - __table_phys_start, BLOCK_2MB);
  __table_virt_end = .;

  . = KERNEL_HEAP_BASE;
  __lma_cursor = ALIGN(__lma_cursor + 4M, BLOCK_2MB);

  .bss.heap : AT(ALIGN(__lma_cursor, BLOCK_2MB)) ALIGN(BLOCK_2MB)
  {
    __heap_start = .;
    KEEP(*(.bss.heap))
  } : kheap
  __heap_start_phys = LOADADDR(.bss.heap);
  __lma_heap_bootstrap_end = __heap_start_phys + . - KERNEL_HEAP_BASE;
  . += HEAP_END;
  __heap_end = .;
  __heap_end_phys = __heap_start_phys + HEAP_END;
  /DISCARD/ : {
    *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*)
  }
}

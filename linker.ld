/* kernel.ld — AArch64, 4 KiB pages, image @ 0x40080000
   Simple: 2 PT_LOAD segments (RX and RW), 4 KiB alignment throughout. */

ENTRY(_start)

PHDRS {
  text PT_LOAD FLAGS(5);  /* R X */
  data PT_LOAD FLAGS(6);  /* R W */
}

SECTIONS
{
  . = 0x40080000;
  __kernel_start = .;

  /* ----- TEXT & RODATA (RX) ----- */
  . = ALIGN(0x1000);

  __text_start = .;
  .text.boot : ALIGN(0x1000) {
    KEEP(*(.text.boot))
  } :text

  /* Exception vectors: keep on a 4 KiB boundary (your MMU maps RX here) */
  . = ALIGN(0x1000);
  __vectors_start = .;
  .text.vectors : {
    KEEP(*(.text.vectors))
  } :text
  __vectors_end = .;

  .text : ALIGN(0x1000) {
    *(.text .text.* .gnu.linkonce.t*)
  } :text
  __text_end = .;

  .rodata : ALIGN(0x1000) {
    *(.rodata .rodata.* .gnu.linkonce.r*)
  } :text
  __rodata_end = .;

  /* ----- DATA (RW) ----- */
  . = ALIGN(0x1000);
  _data = .;

  __data_start = .;
  .data : ALIGN(0x1000) {
    *(.data .data.* .gnu.linkonce.d*)
    *(.init_array .init_array.*)
    *(.fini_array .fini_array.*)
    *(.got .got.*)
  } :data
  __data_end = .;

  /* ----- BSS (RW, no load) ----- */
  . = ALIGN(0x1000);
  __bss_start = .;
  .bss (NOLOAD) : ALIGN(0x1000) {
    *(.bss .bss.*)
    *(COMMON)
  } :data
  __bss_end = .;
  __bss_size = (__bss_end - __bss_start);

  /* Stacks (no load) */
  . = ALIGN(0x1000);
  .bss.stack (NOLOAD) : ALIGN(0x1000) {
    KEEP(*(.bss.stack))
  } :data

  /* Page allocator pool (no load) — still needs RW mapping at runtime */
  . = ALIGN(0x1000);
  __page_pool_start = .;
/*  .page_pool (NOLOAD) : ALIGN(0x1000) {*/
/*    . += 4M;*/
/*  } :data*/
/*  __page_pool_end = .;*/

  . = ALIGN(0x1000);
  __heap_start = .;

  _end = .;
  __kernel_end = .;

  /DISCARD/ : {
    *(.comment) *(.gnu*) *(.note*) *(.eh_frame*) *(.eh_frame_hdr*)
  }
}

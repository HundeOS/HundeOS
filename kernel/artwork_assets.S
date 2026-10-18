    .section .rodata.system_artwork,"a",@progbits
    .balign 8
    .global __hunde_logo_ascii
__hunde_logo_ascii:
    .incbin "assets/logo.ascii"
    .byte 0

    .balign 8
    .global __hunde_panic_ascii
__hunde_panic_ascii:
    .incbin "assets/panic.ascii"
    .byte 0

    .section .note.GNU-stack,"",@progbits

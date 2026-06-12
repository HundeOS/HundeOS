    .section .binaries, "a"
    .global _binaries_start, _binaries_end
_binaries_start:
    .incbin "build/kernel/embedded.fs"
_binaries_end:

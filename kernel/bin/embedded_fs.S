#ifndef EMBEDDED_FS_PATH
#error "EMBEDDED_FS_PATH must identify the target architecture's embedded filesystem"
#endif

    .section .binaries, "a"
    .global _binaries_start, _binaries_end
_binaries_start:
    .incbin EMBEDDED_FS_PATH
_binaries_end:

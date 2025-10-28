// user_blob.S
    .section .binaries, "a"
    .global _binaries_start, _bin_hello_world_start, _bin_hello_world_end, _bin_idle_start, _bin_idle_end, _bin_shell_start, _bin_shell_end, _binaries_end
_binaries_start:
_bin_hello_world_start:
    .incbin "build/user/hello_world.bin"
_bin_hello_world_end:
_bin_idle_start:
    .incbin "build/user/idle.bin"
_bin_idle_end:
_bin_shell_start:
    .incbin "build/user/shell.bin"
_bin_shell_end:
_binaries_end:

// user_blob.S
    .section .binaries, "a"
    .global _binaries_start, _bin_hello_world_start, _bin_hello_world_end
_binaries_start:
_bin_hello_world_start:
    .incbin "build/user/hello_world.bin"
_bin_hello_world_end:
_binaries_end:

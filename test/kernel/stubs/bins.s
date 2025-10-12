// user_blob.S
    .section .text, "a"
    .global __binaries_start, __bin_hello_world_start, __bin_hello_world_end
__binaries_start:
__bin_hello_world_start:
    .incbin "hello_world.bin"
__bin_hello_world_end:
__binaries_end:

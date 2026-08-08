    .section .text, "a"
    .global __embedded_binaries_start, __embedded_binaries_end
    .global ___embedded_binaries_start, ___embedded_binaries_end
    .global __binaries_start, __binaries_end
    .global __bin_hello_world_start, __bin_hello_world_end
__embedded_binaries_start:
___embedded_binaries_start:
__binaries_start:
    .long 0x31534645
    .short 1
    .short 6
    .long embedded_index - __embedded_binaries_start
    .long embedded_names - __embedded_binaries_start
    .long embedded_data - __embedded_binaries_start
    .long __embedded_binaries_end - __embedded_binaries_start
    .long 0
    .long 0

embedded_index:
    .quad __bin_hello_world_start - __embedded_binaries_start
    .quad __bin_hello_world_end - __bin_hello_world_start
    .long embedded_name_hello_world - __embedded_binaries_start
    .long embedded_name_idle - embedded_name_hello_world - 1
    .long 0
    .long 0

    .quad __bin_idle_start - __embedded_binaries_start
    .quad __bin_idle_end - __bin_idle_start
    .long embedded_name_idle - __embedded_binaries_start
    .long embedded_name_shell - embedded_name_idle - 1
    .long 0
    .long 0

    .quad __bin_shell_start - __embedded_binaries_start
    .quad __bin_shell_end - __bin_shell_start
    .long embedded_name_shell - __embedded_binaries_start
    .long embedded_name_virtio_blk - embedded_name_shell - 1
    .long 0
    .long 0

    .quad __bin_virtio_blk_start - __embedded_binaries_start
    .quad __bin_virtio_blk_end - __bin_virtio_blk_start
    .long embedded_name_virtio_blk - __embedded_binaries_start
    .long embedded_name_uart_rx - embedded_name_virtio_blk - 1
    .long 0
    .long 0

    .quad __bin_uart_rx_start - __embedded_binaries_start
    .quad __bin_uart_rx_end - __bin_uart_rx_start
    .long embedded_name_uart_rx - __embedded_binaries_start
    .long embedded_name_uart_tx - embedded_name_uart_rx - 1
    .long 0
    .long 0

    .quad __bin_uart_tx_start - __embedded_binaries_start
    .quad __bin_uart_tx_end - __bin_uart_tx_start
    .long embedded_name_uart_tx - __embedded_binaries_start
    .long embedded_names_end - embedded_name_uart_tx - 1
    .long 0
    .long 0

embedded_names:
embedded_name_hello_world:
    .asciz "hello_world"
embedded_name_idle:
    .asciz "idle"
embedded_name_shell:
    .asciz "shell"
embedded_name_virtio_blk:
    .asciz "drivers/virtio_blk"
embedded_name_uart_rx:
    .asciz "drivers/uart_rx"
embedded_name_uart_tx:
    .asciz "drivers/uart_tx"
embedded_names_end:

    .balign 8
embedded_data:
__bin_hello_world_start:
    .incbin "../stubs/hello_world.bin"
__bin_hello_world_end:
    .balign 8
__bin_idle_start:
    .incbin "../stubs/hello_world.bin"
__bin_idle_end:
    .balign 8
__bin_shell_start:
    .incbin "../stubs/hello_world.bin"
__bin_shell_end:
    .balign 8
__bin_virtio_blk_start:
    .incbin "../stubs/hello_world.bin"
__bin_virtio_blk_end:
    .balign 8
__bin_uart_rx_start:
    .incbin "../stubs/hello_world.bin"
__bin_uart_rx_end:
    .balign 8
__bin_uart_tx_start:
    .incbin "../stubs/hello_world.bin"
__bin_uart_tx_end:
__embedded_binaries_end:
___embedded_binaries_end:
__binaries_end:

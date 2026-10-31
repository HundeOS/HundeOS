#![no_std]
#![no_main]

hunde::driver_component!(
    hunde_virtio_blk::BlockDriver,
    b"Rust VirtIO block driver panicked"
);

#![no_std]
#![no_main]

hunde::driver_component!(
    hunde_virtio_net::NetworkDriver,
    b"Rust VirtIO network driver panicked"
);

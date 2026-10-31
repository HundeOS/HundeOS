#![no_std]
#![no_main]

hunde::driver_component!(
    hunde_virtio_keyboard::Keyboard,
    b"Rust VirtIO keyboard driver panicked"
);

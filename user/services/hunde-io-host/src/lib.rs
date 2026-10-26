#![cfg_attr(feature = "service", no_std)]

extern crate alloc;

mod console;
mod filesystem;
mod host;

#[cfg(feature = "service")]
hunde::executable!(run, b"Rust I/O host panicked");

pub fn run() -> Result<(), hunde::abi::IoError> {
    host::run()
}

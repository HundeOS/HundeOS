#![cfg_attr(feature = "service", no_std)]

extern crate alloc;

mod filesystem;
mod host;
mod socket;
mod streams;

#[cfg(feature = "service")]
hunde::executable!(run, b"Rust I/O host panicked");

pub fn run() -> Result<(), hunde::abi::IoError> {
    host::run()
}

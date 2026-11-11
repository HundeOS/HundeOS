#![no_std]

#[cfg(feature = "alloc")]
extern crate alloc;

pub mod abi;
#[cfg(feature = "alloc")]
pub mod block;
#[cfg(feature = "alloc")]
pub mod block_driver;
#[cfg(feature = "alloc")]
pub mod block_host;
pub mod boot;
#[cfg(feature = "alloc")]
pub mod bootstrap;
#[cfg(feature = "alloc")]
pub mod channel;
pub mod component;
#[cfg(feature = "alloc")]
pub mod devfs;
#[cfg(feature = "alloc")]
pub mod dma;
#[cfg(feature = "alloc")]
pub mod driver;
#[cfg(feature = "alloc")]
pub mod embedfs;
#[cfg(feature = "alloc")]
pub mod fibre;
#[cfg(feature = "alloc")]
pub mod inheritance;
pub mod io;
#[cfg(feature = "alloc")]
pub mod mmio;
#[cfg(feature = "alloc")]
mod native;
#[cfg(feature = "alloc")]
pub mod packet;
pub mod process;
pub mod protocol;
pub mod runtime;
#[cfg(feature = "alloc")]
pub mod service;
pub mod terminal;
#[cfg(feature = "alloc")]
pub mod virtio;
#[cfg(feature = "alloc")]
pub mod wuff;

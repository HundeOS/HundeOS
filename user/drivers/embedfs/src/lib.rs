#![cfg_attr(feature = "component", no_std)]

#[cfg(feature = "component")]
hunde::io_component!(
    hunde::embedfs::Filesystem,
    b"embedfs component panic",
    hunde::embedfs::Filesystem::new
);

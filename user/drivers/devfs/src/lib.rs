#![cfg_attr(feature = "component", no_std)]

#[cfg(feature = "component")]
hunde::io_component!(
    hunde::devfs::Filesystem,
    b"devfs component panic",
    hunde::devfs::Filesystem::new
);

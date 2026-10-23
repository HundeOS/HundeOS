use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("Cargo manifest directory"));
    let root = manifest.ancestors().nth(3).expect("Repository root");
    for input in [
        "tools/generate_rust_io_constants.py",
        "common/ipc/io_protocol.h",
        "common_includes/hunde/io_component.h",
        "common_includes/hunde/boot_info.h",
        "common_includes/hunde/host_component.h",
        "common_includes/syscalls/io.h",
        "common_includes/syscalls/service.h",
        "common_includes/syscalls/mem.h",
    ] {
        println!("cargo::rerun-if-changed={}", root.join(input).display());
    }
    let output =
        PathBuf::from(env::var_os("OUT_DIR").expect("Cargo build output")).join("io_constants.rs");
    let status = Command::new("python3")
        .arg(root.join("tools/generate_rust_io_constants.py"))
        .arg("--output")
        .arg(output)
        .status()
        .expect("Protocol generation requires python3 and clang");
    assert!(
        status.success(),
        "C I/O protocol constant generation failed"
    );
}

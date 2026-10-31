use std::collections::BTreeSet;
use std::env;
use std::path::PathBuf;
use std::process::Command;

fn main() {
    let manifest =
        PathBuf::from(env::var_os("CARGO_MANIFEST_DIR").expect("Cargo manifest directory"));
    let root = manifest.ancestors().nth(3).expect("Repository root");
    let mut approved = BTreeSet::new();
    for (file, bootstrap) in [
        ("config/io-host.conf", false),
        ("config/bootstrap.fstab", true),
    ] {
        let component_manifest = root.join(file);
        println!("cargo::rerun-if-changed={}", component_manifest.display());
        let text =
            std::fs::read_to_string(component_manifest).expect("Bootstrap component manifest");
        for line in text.lines() {
            let fields: Vec<_> = line
                .split('#')
                .next()
                .unwrap_or_default()
                .split_ascii_whitespace()
                .collect();
            if fields.is_empty() {
                continue;
            }
            assert_eq!(
                fields.len(),
                if bootstrap { 4 } else { 3 },
                "Invalid bootstrap component manifest entry"
            );
            let kind = match (bootstrap, fields[0]) {
                (false, "service") | (true, "device") => "Service",
                (false, "filesystem") => "Filesystem",
                (false, "driver") => "Driver",
                _ => panic!("Unknown bootstrap component manifest entry"),
            };
            let path = fields[2];
            let name = path
                .strip_prefix("/sbin/components/")
                .expect("Component image outside bootstrap directory");
            assert!(
                name.ends_with(".wuff")
                    && name.len() > 5
                    && name
                        .bytes()
                        .all(|byte| byte.is_ascii_alphanumeric() || b"._-".contains(&byte)),
                "Invalid bootstrap component image name"
            );
            approved.insert((path.to_string(), kind));
        }
    }
    let registry = approved
        .into_iter()
        .map(|(path, kind)| format!("({path:?}, ImageKind::{kind}),\n"))
        .collect::<String>();
    std::fs::write(
        PathBuf::from(env::var_os("OUT_DIR").expect("Cargo build output")).join("io_components.rs"),
        format!("const APPROVED_IMAGES: &[(&str, ImageKind)] = &[\n{registry}];\n"),
    )
    .expect("Write approved component registry");
    for input in [
        "tools/generate_rust_io_constants.py",
        "common/ipc/io_protocol.h",
        "common/ipc/buffer.h",
        "common/ipc/interrupt_protocol.h",
        "common/hardware/boot_info.h",
        "common_includes/hunde/io_component.h",
        "common_includes/hunde/driver_component.h",
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

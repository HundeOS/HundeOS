from pathlib import Path
import subprocess
import tomllib

import pytest


PROJECT_ROOT = Path(__file__).resolve().parents[2]
WORKSPACE_ROOT = PROJECT_ROOT / "user"


def read_manifest(path):
    return tomllib.loads(path.read_text(encoding="utf-8"))


def test_only_hunde_can_implement_unsafe_rust():
    workspace = read_manifest(WORKSPACE_ROOT / "Cargo.toml")["workspace"]
    assert workspace["lints"]["rust"]["unsafe_code"] == "forbid"
    for member in workspace["members"]:
        manifest = read_manifest(WORKSPACE_ROOT / member / "Cargo.toml")
        if member == "lib/hunde":
            assert manifest["lints"]["rust"]["unsafe_op_in_unsafe_fn"] == "deny"
        else:
            assert manifest["lints"] == {"workspace": True}, member
            assert Path(member).parts[0] in {"services", "drivers", "components"}, member


def test_socket_is_a_component_and_host_is_a_service_executable():
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-", "rust-layout-probe"],
        input=(
            "rust-layout-probe:\n"
            "\t@echo component=$(filter %/socket.bin %/hund-rohr.bin %/memfs.bin %/procfs.bin %/fat32fs.bin %/devfs.bin %/hund-dateisystem.bin %/hund-io-system.bin,$(USER_BINARIES)) "
            "host=$(filter %/hunde-io-host.bin,$(USER_BINARIES))\n"
        ),
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert result.stdout.startswith("component= host=")
    assert result.stdout.strip().endswith("/user/hunde-io-host.bin")


@pytest.mark.parametrize("architecture", ["aarch64", "riscv64"])
def test_boot_archive_depends_on_every_packaged_rust_component(architecture):
    result = subprocess.run(
        ["make", "--no-print-directory", "-sp", "-f", "Makefile", "-f", "-", "component-dependency-probe", f"ARCH={architecture}"],
        input=(
            "component-dependency-probe:\n"
            "\t@printf '%s\\n' archive=$(EMBEDDED_FS) $(filter components/%,$(EMBEDDED_FS_INPUTS))\n"
        ),
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    lines = result.stdout.splitlines()
    archive = next(line.removeprefix("archive=") for line in lines if line.startswith("archive="))
    dependencies = next(line.split(":", 1)[1].split() for line in lines if line.startswith(f"{archive}:"))
    components = [line.split("=", 1)[1] for line in lines if line.startswith("components/") and "=" in line]
    assert components
    for component in components:
        assert component in dependencies, component


@pytest.mark.parametrize("architecture", ["aarch64", "riscv64"])
def test_dynamic_command_creates_output_directory_before_linking(tmp_path, architecture):
    target = tmp_path / "user" / "cat.bin"
    result = subprocess.run(
        [
            "make", "--no-print-directory", "-n", str(target),
            f"ARCH={architecture}", f"PLATFORM_BUILD_ROOT={tmp_path}",
            "USER_CRT_PIC_OBJECT=", "SYSROOT_WUFF_FILES=", "cat_WUFF_OBJECTS=",
            "WUFF_LD=true",
        ],
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert not target.parent.exists()
    create_directory = f"mkdir -p `dirname {target}`"
    link_command = "true -pie --dynamic-linker /sbin/wuff-ld"
    assert create_directory in result.stdout
    assert result.stdout.index(create_directory) < result.stdout.index(link_command)


@pytest.fixture
def consumer(tmp_path):
    policy = read_manifest(WORKSPACE_ROOT / "Cargo.toml")["workspace"]["lints"]["rust"]["unsafe_code"]
    manifest = tmp_path / "Cargo.toml"
    manifest.write_text(
        '[package]\nname = "consumer-policy-probe"\nversion = "0.1.0"\nedition = "2024"\n'
        '[workspace]\n[workspace.lints.rust]\n'
        f'unsafe_code = "{policy}"\n'
        '[lints]\nworkspace = true\n'
        f'[dependencies]\nhunde = {{ path = "{WORKSPACE_ROOT / "lib/hunde"}" }}\n',
        encoding="utf-8",
    )
    source = tmp_path / "src" / "main.rs"
    source.parent.mkdir()
    toolchain = read_manifest(PROJECT_ROOT / "rust-toolchain.toml")["toolchain"]["channel"]

    def check(code):
        source.write_text(code, encoding="utf-8")
        return subprocess.run(
            ["cargo", f"+{toolchain}", "check", "--all-targets", "--offline", "--manifest-path", str(manifest)],
            cwd=tmp_path,
            capture_output=True,
            text=True,
            check=False,
        )

    return check


@pytest.mark.parametrize(
    ("source", "diagnostic"),
    [
        ("fn main() { unsafe {} }", "usage of an `unsafe` block"),
        (
            "fn main() {} #[cfg(test)] mod tests { #[test] fn invalid() { unsafe {} } }",
            "usage of an `unsafe` block",
        ),
        ("unsafe fn raw() {} fn main() {}", "declaration of an `unsafe` function"),
        ('unsafe extern "C" { fn raw(); } fn main() {}', "usage of an `unsafe extern` block"),
        ("#[allow(unsafe_code)] fn main() { unsafe {} }", "incompatible with previous forbid"),
    ],
)
def test_consumers_cannot_write_or_allow_unsafe(consumer, source, diagnostic):
    result = consumer(source)
    assert result.returncode != 0
    assert diagnostic in result.stderr


def test_consumers_can_load_approved_bundled_components(consumer):
    result = consumer(
        "fn main() { let _ = hunde::wuff::LoadedComponent::load(hunde::wuff::BundledComponent::Socket); }"
    )
    assert result.returncode == 0, result.stderr


def test_consumers_cannot_pass_arbitrary_component_paths(consumer):
    result = consumer('fn main() { let _ = hunde::wuff::LoadedComponent::load(b"/tmp/arbitrary.wuff"); }')
    assert result.returncode != 0
    assert "expected `BundledComponent`" in result.stderr


def test_raw_component_loading_is_private(consumer):
    result = consumer('fn main() { let _ = hunde::wuff::LoadedComponent::load_path(b"/tmp/arbitrary.wuff"); }')
    assert result.returncode != 0
    assert "private associated function" in result.stderr

import json
from pathlib import Path
import re
import shutil
import subprocess
import tomllib

import pytest

import generate_rust_io_constants as generator


PROJECT_ROOT = Path(__file__).resolve().parents[2]


@pytest.fixture
def protocol_root(tmp_path):
    protocol = tmp_path / "common/ipc/io_protocol.h"
    protocol.parent.mkdir(parents=True)
    protocol.write_text(
        '#include "syscalls/io.h"\n'
        "#define IO_PROTOCOL_DATA_SIZE 256\n#define IO_DIRECTORY_BATCH_CAPACITY 3\n"
        "#define IO_SESSION_HANDLE_BASE (1LL << 32)\n"
        "typedef enum IORequestOperation {\n"
        "IO_REQUEST_OPEN = (1 << 3) + 1, IO_REQUEST_ACCEPT, IO_REQUEST_READ,\n"
        "IO_REQUEST_WRITE, IO_REQUEST_CLOSE, IO_REQUEST_RELEASE_CLIENT\n"
        "} IORequestOperation;\n",
        encoding="utf-8",
    )
    status = tmp_path / "common_includes/syscalls/io.h"
    status.parent.mkdir(parents=True)
    status.write_text(
        "typedef enum IOStatus { IO_OK = 0, IO_NO_DEVICE_FOUND = -1, IO_PANIC = -255 } IOStatus;\n"
        "typedef enum IOFlags { IO_OPEN_READ = 1 << 2, IO_OPEN_WRITE = 1 << 3,\n"
        "IO_OPEN_BIND = 0x10, IO_OPEN_LISTEN = IO_OPEN_BIND << 1 } IOFlags;\n"
        "typedef enum IOTransferFlags { IO_TRANSFER_NONBLOCK = 1, IO_TRANSFER_APPEND = 2 } IOTransferFlags;\n"
        "typedef enum IOSeekWhence { IO_SEEK_SET, IO_SEEK_CURRENT, IO_SEEK_END } IOSeekWhence;\n"
        "typedef enum IOFileChangeFlags { IO_FILE_CHANGE_WRITE = 1, IO_FILE_CHANGE_DELETE = 2,\n"
        "IO_FILE_CHANGE_RENAME = 4, IO_FILE_CHANGE_TRUNCATE = 8 } IOFileChangeFlags;\n"
        "#define IO_FILE_WATCH_CAPACITY 16U\n"
        "#define IO_MODE_TYPE_FILE 0x8000U\n#define IO_MODE_TYPE_DIR 0x4000U\n#define IO_DIRENT_NAME_MAX 256\n",
        encoding="utf-8",
    )
    status.with_name("service.h").write_text(
        "typedef enum ServiceEventType { SERVICE_EVENT_MEMORY_PRESSURE = 6 } ServiceEventType;\n",
        encoding="utf-8",
    )
    status.with_name("mem.h").write_text(
        "typedef enum MemPressureLevel { MEM_PRESSURE_NORMAL, MEM_PRESSURE_LOW, MEM_PRESSURE_CRITICAL } MemPressureLevel;\n",
        encoding="utf-8",
    )
    component = tmp_path / "common_includes/hunde/io_component.h"
    component.parent.mkdir(parents=True)
    component.write_text("#define HUNDE_IO_COMPONENT_VERSION 6U\n", encoding="utf-8")
    return tmp_path


def test_clang_evaluates_expressions_implicit_values_and_signed_statuses(protocol_root):
    assert generator.compile_enum(protocol_root, "IORequestOperation", "u32") == [
        ("IO_REQUEST_OPEN", 9),
        ("IO_REQUEST_ACCEPT", 10),
        ("IO_REQUEST_READ", 11),
        ("IO_REQUEST_WRITE", 12),
        ("IO_REQUEST_CLOSE", 13),
        ("IO_REQUEST_RELEASE_CLIENT", 14),
    ]
    generated = generator.generate(protocol_root)
    assert "pub const IO_NO_DEVICE_FOUND: i32 = -1;" in generated
    assert "pub const IO_PANIC: i32 = -255;" in generated
    assert "pub const IO_OPEN_READ: u64 = 4;" in generated
    assert "pub const IO_OPEN_LISTEN: u64 = 32;" in generated
    assert "pub const IO_FILE_CHANGE_TRUNCATE: u64 = 8;" in generated
    assert "pub const IO_FILE_WATCH_CAPACITY: u64 = 16;" in generated
    assert "pub const IO_SESSION_HANDLE_BASE: u64 = 4294967296;" in generated
    assert "pub const HUNDE_IO_COMPONENT_VERSION: u64 = 6;" in generated
    assert "pub const SERVICE_EVENT_MEMORY_PRESSURE: u32 = 6;" in generated
    assert "pub const MEM_PRESSURE_CRITICAL: u64 = 2;" in generated


def test_regeneration_tracks_c_changes_including_following_implicit_operations(protocol_root):
    original = generator.generate(protocol_root)
    header = protocol_root / "common/ipc/io_protocol.h"
    header.write_text(header.read_text(encoding="utf-8").replace("(1 << 3) + 1", "41"), encoding="utf-8")
    regenerated = generator.generate(protocol_root)
    assert regenerated != original
    assert "pub const IO_REQUEST_OPEN: u32 = 41;" in regenerated
    assert "pub const IO_REQUEST_ACCEPT: u32 = 42;" in regenerated


@pytest.mark.parametrize(
    ("enum_name", "rust_type", "expression"),
    [("IORequestOperation", "u32", "1ULL << 32"), ("IOStatus", "i32", "1LL << 32"), ("IOFlags", "u64", "-1")],
)
def test_values_outside_the_wire_field_range_fail(protocol_root, enum_name, rust_type, expression):
    header = protocol_root / "common/ipc/io_protocol.h"
    header.write_text(f"typedef enum {enum_name} {{ INVALID = {expression} }} {enum_name};\n", encoding="utf-8")
    with pytest.raises(ValueError, match=f"does not fit {rust_type}"):
        generator.compile_enum(protocol_root, enum_name, rust_type)


def test_missing_enum_or_invalid_c_fails_instead_of_emitting_stale_constants(protocol_root):
    with pytest.raises(ValueError, match="Expected one definition"):
        generator.compile_enum(protocol_root, "MissingEnum", "u32")
    header = protocol_root / "common/ipc/io_protocol.h"
    header.write_text("typedef enum IORequestOperation { INVALID = undefined_value } IORequestOperation;", encoding="utf-8")
    with pytest.raises(ValueError, match="Clang could not evaluate IORequestOperation"):
        generator.generate(protocol_root)


def test_unknown_explicit_initializer_ast_is_not_treated_as_implicit_increment():
    declaration = {
        "kind": "EnumDecl",
        "name": "IOStatus",
        "inner": [{"kind": "EnumConstantDecl", "name": "IO_OK", "inner": [{"kind": "UnknownExpr"}]}],
    }
    with pytest.raises(ValueError, match="Clang did not evaluate IOStatus.IO_OK"):
        generator.enum_values(json.dumps(declaration), "IOStatus", "i32")


def test_all_generated_repository_constants_equal_the_c_compilers_values(tmp_path):
    generated = generator.generate(PROJECT_ROOT)
    constants = re.findall(r"pub const (\w+): (u32|i32|u64) = (-?\d+);", generated)
    assert constants
    assertions = []
    for name, rust_type, value in constants:
        suffix = {"u32": "U", "u64": "ULL", "i32": ""}[rust_type]
        assertions.append(f'_Static_assert(({name}) == ({value}{suffix}), "{name}");')
    source = tmp_path / "protocol_values.c"
    source.write_text('#include "common/ipc/io_protocol.h"\n#include "common_includes/hunde/io_component.h"\n#include "common_includes/syscalls/service.h"\n#include "common_includes/syscalls/mem.h"\n' + "\n".join(assertions), encoding="utf-8")
    result = subprocess.run(
        ["cc", "-std=gnu11", "-fsyntax-only", "-I", str(PROJECT_ROOT),
         "-I", str(PROJECT_ROOT / "common"), "-I", str(PROJECT_ROOT / "common_includes"),
         "-I", str(PROJECT_ROOT / "user/lib"), "-I", str(PROJECT_ROOT / "user/lib/fiber/arch/hosted"), str(source)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr


@pytest.mark.parametrize(
    ("header_path", "original", "replacement", "expected"),
    [
        ("common/ipc/io_protocol.h", "(1 << 3) + 1", "41", "pub const IO_REQUEST_ACCEPT: u32 = 42;"),
        ("common_includes/syscalls/service.h", "= 6", "= 19", "pub const SERVICE_EVENT_MEMORY_PRESSURE: u32 = 19;"),
        ("common_includes/syscalls/mem.h", "MEM_PRESSURE_LOW,", "MEM_PRESSURE_LOW = 7,", "pub const MEM_PRESSURE_CRITICAL: u64 = 8;"),
        ("common_includes/hunde/io_component.h", "6U", "7U", "pub const HUNDE_IO_COMPONENT_VERSION: u64 = 7;"),
    ],
)
def test_cargo_regenerates_constants_after_a_c_header_change(protocol_root, header_path, original, replacement, expected):
    crate = protocol_root / "user/lib/hunde"
    (crate / "src").mkdir(parents=True)
    crate.joinpath("Cargo.toml").write_text(
        '[package]\nname = "hunde-protocol-probe"\nversion = "0.1.0"\nedition = "2024"\n',
        encoding="utf-8",
    )
    crate.joinpath("src/lib.rs").write_text(
        '#![no_std]\ninclude!(concat!(env!("OUT_DIR"), "/io_constants.rs"));\n', encoding="utf-8"
    )
    shutil.copyfile(PROJECT_ROOT / "user/lib/hunde/build.rs", crate / "build.rs")
    (protocol_root / "tools").mkdir()
    shutil.copyfile(PROJECT_ROOT / "tools/generate_rust_io_constants.py", protocol_root / "tools/generate_rust_io_constants.py")
    toolchain = tomllib.loads((PROJECT_ROOT / "rust-toolchain.toml").read_text(encoding="utf-8"))["toolchain"]["channel"]
    command = ["cargo", f"+{toolchain}", "check", "--offline", "--manifest-path", str(crate / "Cargo.toml")]
    first = subprocess.run(command, cwd=protocol_root, capture_output=True, text=True, check=False)
    assert first.returncode == 0, first.stderr
    output = next((crate / "target/debug/build").glob("hunde-protocol-probe-*/out/io_constants.rs"))
    assert "pub const IO_REQUEST_OPEN: u32 = 9;" in output.read_text(encoding="utf-8")
    header = protocol_root / header_path
    header.write_text(header.read_text(encoding="utf-8").replace(original, replacement), encoding="utf-8")
    second = subprocess.run(command, cwd=protocol_root, capture_output=True, text=True, check=False)
    assert second.returncode == 0, second.stderr
    assert expected in output.read_text(encoding="utf-8")


def test_make_tracks_the_generator_build_script_and_c_headers():
    result = subprocess.run(
        ["make", "--no-print-directory", "-s", "-f", "Makefile", "-f", "-", "protocol-dependencies"],
        input="protocol-dependencies:\n\t@printf '%s\\n' $(RUST_IO_SOURCES)\n",
        cwd=PROJECT_ROOT,
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    assert {
        "common/ipc/io_protocol.h",
        "common_includes/syscalls/io.h",
        "common_includes/syscalls/service.h",
        "common_includes/syscalls/mem.h",
        "tools/generate_rust_io_constants.py",
        "user/lib/hunde/build.rs",
    } <= set(result.stdout.splitlines())

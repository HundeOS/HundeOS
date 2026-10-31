"""Generate Rust I/O constants from Clang's evaluated C enum declarations."""

import argparse
import json
from pathlib import Path
import subprocess


ENUM_TYPES = (
    ("IORequestOperation", "u32"),
    ("IOStatus", "i32"),
    ("IOFlags", "u64"),
    ("IOTransferFlags", "u64"),
    ("IOSeekWhence", "u64"),
    ("IOFileChangeFlags", "u64"),
)
RUST_RANGES = {
    "u32": (0, (1 << 32) - 1),
    "i32": (-(1 << 31), (1 << 31) - 1),
    "u64": (0, (1 << 64) - 1),
}


def declarations(source):
    decoder = json.JSONDecoder()
    remaining = source.lstrip()
    while remaining:
        declaration, end = decoder.raw_decode(remaining)
        yield declaration
        remaining = remaining[end:].lstrip()


def constant_expressions(node):
    if node.get("kind") == "ConstantExpr" and "value" in node:
        yield node["value"]
    for child in node.get("inner", []):
        yield from constant_expressions(child)


def enum_values(source, enum_name, rust_type):
    matching = [
        node for node in declarations(source)
        if node.get("kind") == "EnumDecl" and node.get("name") == enum_name and node.get("inner")
    ]
    if len(matching) != 1:
        raise ValueError(f"Expected one definition of {enum_name}, found {len(matching)}")
    minimum, maximum = RUST_RANGES[rust_type]
    result = []
    previous = -1
    for entry in matching[0]["inner"]:
        if entry.get("kind") != "EnumConstantDecl":
            continue
        name = entry["name"]
        if entry.get("inner"):
            evaluated = next(constant_expressions(entry), None)
            if evaluated is None:
                raise ValueError(f"Clang did not evaluate {enum_name}.{name}")
            value = int(evaluated)
        else:
            value = previous + 1
        if not minimum <= value <= maximum:
            raise ValueError(f"{enum_name}.{name}={value} does not fit {rust_type}")
        result.append((name, value))
        previous = value
    if not result:
        raise ValueError(f"No constants found in {enum_name}")
    return result


def compile_enum(root, enum_name, rust_type, source=None):
    includes = (root, root / "common", root / "common_includes", root / "user/lib",
                root / "user/lib/fiber/arch/hosted")
    command = ["clang", "-x", "c", "-std=gnu11", "-fsyntax-only"]
    for directory in includes:
        command.extend(["-I", str(directory)])
    command.extend([
        "-Xclang", "-ast-dump=json", "-Xclang", f"-ast-dump-filter={enum_name}",
        "-" if source is not None else str(root / "common/ipc/io_protocol.h"),
    ])
    result = subprocess.run(command, input=source, capture_output=True, text=True, check=False)
    if result.returncode != 0:
        raise ValueError(f"Clang could not evaluate {enum_name}:\n{result.stderr}")
    return enum_values(result.stdout, enum_name, rust_type)


def generate(root):
    lines = []
    for enum_name, rust_type in ENUM_TYPES:
        for name, value in compile_enum(root, enum_name, rust_type):
            lines.append(f"pub const {name}: {rust_type} = {value};")
    for enum_name, rust_type, header in (
        ("ServiceEventType", "u32", "common_includes/syscalls/service.h"),
        ("MemPressureLevel", "u64", "common_includes/syscalls/mem.h"),
        ("ServiceCode", "i32", "common_includes/syscalls/service.h"),
        ("InterruptProtocolRequestKind", "u32", "common/ipc/interrupt_protocol.h"),
        ("InterruptProtocolResponseKind", "u32", "common/ipc/interrupt_protocol.h"),
        ("InterruptProtocolCode", "i32", "common/ipc/interrupt_protocol.h"),
        ("UartKind", "u64", "common/hardware/boot_info.h"),
        ("SdHostKind", "u64", "common/hardware/boot_info.h"),
        ("BufferDirection", "u32", "common/ipc/buffer.h"),
    ):
        source = "#include " + json.dumps(str(root / header)) + "\n"
        for name, value in compile_enum(root, enum_name, rust_type, source):
            lines.append(f"pub const {name}: {rust_type} = {value};")
    names = ("IO_MODE_TYPE_FILE", "IO_MODE_TYPE_DIR", "IO_DIRENT_NAME_MAX", "IO_DIRECTORY_BATCH_CAPACITY", "IO_PROTOCOL_DATA_SIZE", "IO_FILE_WATCH_CAPACITY", "IO_SESSION_HANDLE_BASE", "HUNDE_IO_COMPONENT_VERSION")
    names += ("IO_MODE_TYPE_CHAR", "IO_TERMINAL_INPUT_CR_TO_NL", "IO_TERMINAL_INPUT_START_STOP_OUTPUT",
              "IO_TERMINAL_OUTPUT_PROCESSING", "IO_TERMINAL_OUTPUT_NL_TO_CR_NL", "IO_TERMINAL_CONTROL_CHARACTER_SIZE_8",
              "IO_TERMINAL_LOCAL_SIGNALS", "IO_TERMINAL_LOCAL_CANONICAL", "IO_TERMINAL_LOCAL_ECHO",
              "IO_TERMINAL_LOCAL_ECHO_ERASE", "IO_TERMINAL_LOCAL_ECHO_KILL", "IO_TERMINAL_LOCAL_EXTENDED",
              "IO_TERMINAL_SPEED_38400")
    source = "#include " + json.dumps(str(root / "common/ipc/io_protocol.h")) + "\n#include \"hunde/io_component.h\"\n" + "enum HundeIOMacros { " + ", ".join(
        f"HUNDE_CONST_{name} = {name}" for name in names
    ) + " };\n"
    for name, value in compile_enum(root, "HundeIOMacros", "u64", source):
        lines.append(f"pub const {name.removeprefix('HUNDE_CONST_')}: u64 = {value};")
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        generated = generate(Path(__file__).resolve().parents[1])
        args.output.write_text(generated, encoding="utf-8")
    except (OSError, ValueError) as error:
        parser.error(str(error))


if __name__ == "__main__":
    main()

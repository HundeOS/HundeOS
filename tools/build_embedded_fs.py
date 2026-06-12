#!/usr/bin/env python3
import argparse
import os
import struct


HEADER_FORMAT = "<IHHIIIIII"
ENTRY_FORMAT = "<QQIIII"

HEADER_SIZE = struct.calcsize(HEADER_FORMAT)
ENTRY_SIZE = struct.calcsize(ENTRY_FORMAT)

MAGIC = 0x31534645
VERSION = 1
ALIGNMENT = 8


def align(value: int, alignment: int) -> int:
    remainder = value % alignment
    if remainder == 0:
        return value
    return value + alignment - remainder


def parse_file_arg(arg: str) -> tuple[str, str]:
    if "=" in arg:
        name, path = arg.split("=", 1)
    else:
        path = arg
        name = os.path.splitext(os.path.basename(path))[0]

    if not name:
        raise ValueError(f"empty embedded file name for {arg}")

    return name, path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Build a self-indexing embedded filesystem blob."
    )
    parser.add_argument("output", help="Path to write the generated filesystem blob.")
    parser.add_argument(
        "files",
        metavar="NAME=PATH|PATH",
        nargs="+",
        help="File to embed. If NAME= is omitted, the basename without extension is used.",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = args.output
    files = [parse_file_arg(arg) for arg in args.files]

    names = bytearray()
    entries = []
    file_contents = []

    index_offset = HEADER_SIZE
    names_offset = index_offset + len(files) * ENTRY_SIZE

    for name, path in files:
        with open(path, "rb") as source:
            data = source.read()

        encoded_name = name.encode("utf-8")
        name_offset = names_offset + len(names)
        names.extend(encoded_name)
        names.append(0)
        file_contents.append((data, name_offset, len(encoded_name)))

    data_offset = align(names_offset + len(names), ALIGNMENT)
    file_data = bytearray(b"\0" * (data_offset - (names_offset + len(names))))

    for data, name_offset, name_len in file_contents:
        file_offset = names_offset + len(names) + len(file_data)
        data_file_offset = align(file_offset, ALIGNMENT)
        padding = data_file_offset - file_offset
        file_data.extend(b"\0" * padding)
        file_data.extend(data)

        entries.append((data_file_offset, len(data), name_offset, name_len, 0, 0))

    blob_size = names_offset + len(names) + len(file_data)

    header = struct.pack(
        HEADER_FORMAT,
        MAGIC,
        VERSION,
        len(entries),
        index_offset,
        names_offset,
        data_offset,
        blob_size,
        0,
        0,
    )

    index = b"".join(struct.pack(ENTRY_FORMAT, *entry) for entry in entries)
    blob = header + index + names + file_data

    os.makedirs(os.path.dirname(output), exist_ok=True)
    with open(output, "wb") as target:
        target.write(blob)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

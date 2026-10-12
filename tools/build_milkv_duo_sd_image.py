#!/usr/bin/env python3
import argparse
from dataclasses import dataclass
import os
from pathlib import Path
import shutil
import struct


SECTOR_SIZE = 512
PARTITION_TABLE_OFFSET = 446
PARTITION_ENTRY_SIZE = 16
MBR_SIGNATURE = b"\x55\xaa"


@dataclass(frozen=True)
class Partition:
    bootable: bool
    partition_type: int
    start_sector: int
    sector_count: int


def read_partition(mbr: bytes, index: int) -> Partition:
    offset = PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE
    entry = mbr[offset:offset + PARTITION_ENTRY_SIZE]
    if len(entry) != PARTITION_ENTRY_SIZE:
        raise RuntimeError("truncated MBR partition table")
    return Partition(
        bootable=entry[0] == 0x80,
        partition_type=entry[4],
        start_sector=struct.unpack_from("<I", entry, 8)[0],
        sector_count=struct.unpack_from("<I", entry, 12)[0],
    )


def chs(lba: int) -> bytes:
    sectors_per_track = 63
    heads = 255
    cylinder = lba // (sectors_per_track * heads)
    if cylinder > 1023:
        return b"\xfe\xff\xff"
    remainder = lba % (sectors_per_track * heads)
    head = remainder // sectors_per_track
    sector = remainder % sectors_per_track + 1
    return bytes((head, (sector & 0x3F) | ((cylinder >> 2) & 0xC0), cylinder & 0xFF))


def write_partition(mbr: bytearray, index: int, partition: Partition) -> None:
    offset = PARTITION_TABLE_OFFSET + index * PARTITION_ENTRY_SIZE
    end_sector = partition.start_sector + partition.sector_count - 1
    entry = bytearray(PARTITION_ENTRY_SIZE)
    entry[0] = 0x80 if partition.bootable else 0
    entry[1:4] = chs(partition.start_sector)
    entry[4] = partition.partition_type
    entry[5:8] = chs(end_sector)
    struct.pack_into("<I", entry, 8, partition.start_sector)
    struct.pack_into("<I", entry, 12, partition.sector_count)
    mbr[offset:offset + PARTITION_ENTRY_SIZE] = entry


def copy_exact(source, target, byte_count: int) -> None:
    remaining = byte_count
    while remaining > 0:
        chunk = source.read(min(1024 * 1024, remaining))
        if not chunk:
            raise RuntimeError("input ended before the declared partition size")
        target.write(chunk)
        remaining -= len(chunk)


def build_image(
    template: Path,
    boot_partition: Path,
    root_partition: Path,
    output: Path,
    boot_config_partition: Path | None = None,
) -> None:
    with template.open("rb") as source:
        mbr = bytearray(source.read(SECTOR_SIZE))
    if len(mbr) != SECTOR_SIZE or bytes(mbr[-2:]) != MBR_SIGNATURE:
        raise RuntimeError("firmware template does not contain a valid MBR")

    boot = read_partition(mbr, 0)
    partitions = [read_partition(mbr, index) for index in range(4)]
    linux_partitions = [
        (index, partition)
        for index, partition in enumerate(partitions)
        if partition.partition_type == 0x83 and partition.sector_count > 0
    ]
    if len(linux_partitions) != 1:
        raise RuntimeError("firmware template must contain exactly one Linux root partition")
    root_index, root = linux_partitions[0]
    boot_size = boot_partition.stat().st_size
    root_size = root_partition.stat().st_size
    if not boot.bootable or boot.partition_type != 0x0C or boot.start_sector != 1:
        raise RuntimeError("firmware template does not use the Milk-V Duo boot partition layout")
    if boot_size != boot.sector_count * SECTOR_SIZE:
        raise RuntimeError("boot partition size does not match the firmware MBR")
    boot_end_sector = boot.start_sector + boot.sector_count
    if root.start_sector < boot_end_sector:
        raise RuntimeError("firmware template root partition overlaps the boot partition")
    root_offset = root.start_sector * SECTOR_SIZE
    if template.stat().st_size < root_offset:
        raise RuntimeError("firmware template ends before the root partition")
    if root_size == 0 or root_size % SECTOR_SIZE != 0:
        raise RuntimeError("root filesystem size must be a non-zero multiple of 512 bytes")

    boot_config = None
    root_start_sector = root.start_sector
    if boot_config_partition is not None:
        boot_config_size = boot_config_partition.stat().st_size
        if boot_config_size == 0 or boot_config_size % SECTOR_SIZE != 0:
            raise RuntimeError("boot configuration filesystem size must be a non-zero multiple of 512 bytes")
        boot_config = Partition(False, 0x83, root.start_sector, boot_config_size // SECTOR_SIZE)
        root_start_sector = boot_config.start_sector + boot_config.sector_count
        write_partition(mbr, 1, boot_config)

    root = Partition(False, 0x83, root_start_sector, root_size // SECTOR_SIZE)
    write_partition(mbr, root_index, root)

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with temporary.open("wb") as target:
            target.write(mbr)
            with boot_partition.open("rb") as source:
                copy_exact(source, target, boot_size)
            preserved_size = root_offset - target.tell()
            if preserved_size > 0:
                with template.open("rb") as source:
                    source.seek(target.tell())
                    copy_exact(source, target, preserved_size)
            if boot_config is not None:
                with boot_config_partition.open("rb") as source:
                    copy_exact(source, target, boot_config.sector_count * SECTOR_SIZE)
            with root_partition.open("rb") as source:
                shutil.copyfileobj(source, target, 1024 * 1024)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Assemble a flashable Milk-V Duo-family SD-card image.")
    parser.add_argument("template", type=Path, help="Official image prefix containing the MBR.")
    parser.add_argument("boot_partition", type=Path, help="128 MiB FAT16 partition with fip.bin and boot.sd.")
    parser.add_argument("root_partition", type=Path, help="HundeOS ext2 root filesystem image.")
    parser.add_argument("output", type=Path)
    parser.add_argument("--boot-config", type=Path, help="Optional ext2 boot-configuration partition.")
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    build_image(args.template, args.boot_partition, args.root_partition, args.output, args.boot_config)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())

#!/usr/bin/env python3
import argparse
import os
from pathlib import Path
import shutil

from mbr import MBR_SIGNATURE, Partition, SECTOR_SIZE, write_partition


CARD_SIZE = 256 * 1024 * 1024
FIRST_PARTITION_SECTOR = 2048


def build_image(boot: Path, boot_config: Path, root: Path, output: Path) -> None:
    images = (boot, boot_config, root)
    sizes = tuple(image.stat().st_size for image in images)
    if any(size == 0 or size % SECTOR_SIZE != 0 for size in sizes):
        raise RuntimeError("partition images must be nonempty and sector aligned")

    starts = (FIRST_PARTITION_SECTOR,
              FIRST_PARTITION_SECTOR + sizes[0] // SECTOR_SIZE,
              FIRST_PARTITION_SECTOR + (sizes[0] + sizes[1]) // SECTOR_SIZE)
    if starts[2] + sizes[2] // SECTOR_SIZE > CARD_SIZE // SECTOR_SIZE:
        raise RuntimeError("partition images exceed the QEMU SD card")

    mbr = bytearray(SECTOR_SIZE)
    for index, (start, size, kind) in enumerate(zip(starts, sizes, (0x0C, 0x83, 0x83))):
        write_partition(mbr, index, Partition(index == 0, kind, start, size // SECTOR_SIZE))
    mbr[-2:] = MBR_SIGNATURE

    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(output.suffix + ".tmp")
    try:
        with temporary.open("wb") as target:
            target.truncate(CARD_SIZE)
            target.write(mbr)
            for image, start in zip(images, starts):
                target.seek(start * SECTOR_SIZE)
                with image.open("rb") as source:
                    shutil.copyfileobj(source, target, 1024 * 1024)
        os.replace(temporary, output)
    finally:
        if temporary.exists():
            temporary.unlink()


def main() -> None:
    parser = argparse.ArgumentParser(description="Build the Raspberry Pi QEMU SD card.")
    parser.add_argument("boot", type=Path)
    parser.add_argument("boot_config", type=Path)
    parser.add_argument("root", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    build_image(args.boot, args.boot_config, args.root, args.output)


if __name__ == "__main__":
    main()

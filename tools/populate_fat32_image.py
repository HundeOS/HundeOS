#!/usr/bin/env python3
import argparse
import os
import struct


FAT32_END_OF_CHAIN = 0x0FFFFFFF
FAT32_ATTR_ARCHIVE = 0x20
FAT32_ATTR_LFN = 0x0F
DIR_ENTRY_SIZE = 32


def read_u16(data: bytes, offset: int) -> int:
    return struct.unpack_from("<H", data, offset)[0]


def read_u32(data: bytes, offset: int) -> int:
    return struct.unpack_from("<I", data, offset)[0]


def write_u16(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<H", data, offset, value)


def write_u32(data: bytearray, offset: int, value: int) -> None:
    struct.pack_into("<I", data, offset, value)


def encode_83_name(name: str) -> bytes:
    stem, dot, ext = name.upper().partition(".")
    if not stem or len(stem) > 8 or (dot and len(ext) > 3):
        raise ValueError(f"{name} is not an 8.3 FAT name")

    encoded = bytearray(b" " * 11)
    encoded[0:len(stem)] = stem.encode("ascii")
    encoded[8:8 + len(ext)] = ext.encode("ascii")
    return bytes(encoded)


def is_83_name(name: str) -> bool:
    try:
        encode_83_name(name)
    except ValueError:
        return False
    return name.upper() == name


def short_name_checksum(short_name: bytes) -> int:
    checksum = 0
    for ch in short_name:
        checksum = (((checksum & 1) << 7) + (checksum >> 1) + ch) & 0xFF
    return checksum


def lfn_utf16_units(name: str) -> list[int]:
    encoded = name.encode("utf-16le")
    units = [encoded[i] | (encoded[i + 1] << 8) for i in range(0, len(encoded), 2)]
    units.append(0)
    while len(units) % 13 != 0:
        units.append(0xFFFF)
    return units


def build_lfn_entries(long_name: str, short_name: bytes) -> list[bytes]:
    units = lfn_utf16_units(long_name)
    chunks = [units[i:i + 13] for i in range(0, len(units), 13)]
    checksum = short_name_checksum(short_name)
    entries: list[bytes] = []

    for index in range(len(chunks), 0, -1):
        chunk = chunks[index - 1]
        entry = bytearray(b"\0" * DIR_ENTRY_SIZE)
        entry[0] = index
        if index == len(chunks):
            entry[0] |= 0x40
        entry[11] = FAT32_ATTR_LFN
        entry[13] = checksum

        positions = [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
        for pos, unit in zip(positions, chunk):
            write_u16(entry, pos, unit)
        entries.append(bytes(entry))

    return entries


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Copy a small test file into a FAT32 image.")
    parser.add_argument("image", help="FAT32 image to update in place.")
    parser.add_argument("fat_name", help="8.3 name to create in the image root directory.")
    parser.add_argument("source", help="Host file to copy into the image.")
    return parser.parse_args()


def find_free_clusters(
    image: bytearray,
    fat_offset: int,
    bytes_per_sector: int,
    sectors_per_fat: int,
    count: int,
) -> list[int]:
    fat_size = bytes_per_sector * sectors_per_fat
    clusters: list[int] = []
    for cluster in range(3, fat_size // 4):
        value = read_u32(image, fat_offset + cluster * 4) & 0x0FFFFFFF
        if value == 0:
            clusters.append(cluster)
            if len(clusters) == count:
                return clusters
    raise RuntimeError("no free FAT32 cluster found")


def update_fsinfo(
    image: bytearray,
    bytes_per_sector: int,
    fsinfo_sector: int,
    fat_offset: int,
    cluster_count: int,
) -> None:
    fsinfo_offset = fsinfo_sector * bytes_per_sector
    if fsinfo_offset + bytes_per_sector > len(image):
        return

    if read_u32(image, fsinfo_offset) != 0x41615252:
        return
    if read_u32(image, fsinfo_offset + 484) != 0x61417272:
        return

    free_clusters = 0
    next_free = 0xFFFFFFFF
    for cluster in range(2, cluster_count + 2):
        value = read_u32(image, fat_offset + cluster * 4) & 0x0FFFFFFF
        if value == 0:
            free_clusters += 1
            if next_free == 0xFFFFFFFF:
                next_free = cluster

    write_u32(image, fsinfo_offset + 488, free_clusters)
    write_u32(image, fsinfo_offset + 492, next_free)


def main() -> int:
    args = parse_args()
    with open(args.source, "rb") as source:
        content = source.read()

    with open(args.image, "rb") as image_file:
        image = bytearray(image_file.read())

    bytes_per_sector = read_u16(image, 11)
    sectors_per_cluster = image[13]
    reserved_sectors = read_u16(image, 14)
    fat_count = image[16]
    sectors_per_fat = read_u32(image, 36)
    root_cluster = read_u32(image, 44)
    fsinfo_sector = read_u16(image, 48)
    total_sectors = read_u32(image, 32)
    cluster_size = bytes_per_sector * sectors_per_cluster

    if bytes_per_sector != 512 or sectors_per_cluster == 0 or fat_count == 0 or sectors_per_fat == 0:
        raise RuntimeError("unsupported FAT32 geometry")
    fat_offset = reserved_sectors * bytes_per_sector
    data_offset = (reserved_sectors + fat_count * sectors_per_fat) * bytes_per_sector
    cluster_count = (total_sectors - (reserved_sectors + fat_count * sectors_per_fat)) // sectors_per_cluster
    root_offset = data_offset + (root_cluster - 2) * cluster_size
    needed_clusters = max(1, (len(content) + cluster_size - 1) // cluster_size)
    file_clusters = find_free_clusters(image, fat_offset, bytes_per_sector, sectors_per_fat, needed_clusters)
    file_cluster = file_clusters[0]

    if is_83_name(args.fat_name):
        short_display_name = args.fat_name
    else:
        short_display_name = "LONGFI~1.TXT"
        alias_index = 1
        while True:
            alias_name = encode_83_name(short_display_name)
            alias_exists = False
            for offset in range(root_offset, root_offset + cluster_size, DIR_ENTRY_SIZE):
                if image[offset] == 0x00:
                    break
                if image[offset] != 0xE5 and bytes(image[offset:offset + 11]) == alias_name:
                    alias_exists = True
                    break
            if not alias_exists:
                break
            alias_index += 1
            if alias_index > 9:
                raise RuntimeError("no free long-name short alias found")
            short_display_name = f"LONGFI~{alias_index}.TXT"
    name = encode_83_name(short_display_name)
    lfn_entries = [] if short_display_name == args.fat_name else build_lfn_entries(args.fat_name, name)
    required_entries = len(lfn_entries) + 1
    entry_offset = None
    free_run = 0
    run_start = root_offset
    for offset in range(root_offset, root_offset + cluster_size, DIR_ENTRY_SIZE):
        first = image[offset]
        if first == 0x00 or first == 0xE5:
            if free_run == 0:
                run_start = offset
            free_run += 1
            if free_run >= required_entries:
                entry_offset = run_start
                break
        else:
            free_run = 0
            if bytes(image[offset:offset + 11]) == name:
                entry_offset = offset - (len(lfn_entries) * DIR_ENTRY_SIZE)
                break

    if entry_offset is None:
        raise RuntimeError("no free root directory entry found")

    for cluster_index, cluster in enumerate(file_clusters):
        next_value = FAT32_END_OF_CHAIN
        if cluster_index + 1 < len(file_clusters):
            next_value = file_clusters[cluster_index + 1]
        for fat_index in range(fat_count):
            fat_entry = fat_offset + (fat_index * sectors_per_fat * bytes_per_sector) + cluster * 4
            write_u32(image, fat_entry, next_value)

    for cluster_index, cluster in enumerate(file_clusters):
        file_offset = data_offset + (cluster - 2) * cluster_size
        start = cluster_index * cluster_size
        end = min(start + cluster_size, len(content))
        image[file_offset:file_offset + cluster_size] = b"\0" * cluster_size
        image[file_offset:file_offset + (end - start)] = content[start:end]

    for index, lfn_entry in enumerate(lfn_entries):
        offset = entry_offset + index * DIR_ENTRY_SIZE
        image[offset:offset + DIR_ENTRY_SIZE] = lfn_entry

    short_entry_offset = entry_offset + len(lfn_entries) * DIR_ENTRY_SIZE
    image[short_entry_offset:short_entry_offset + DIR_ENTRY_SIZE] = b"\0" * DIR_ENTRY_SIZE
    image[short_entry_offset:short_entry_offset + 11] = name
    image[short_entry_offset + 11] = FAT32_ATTR_ARCHIVE
    write_u16(image, short_entry_offset + 20, (file_cluster >> 16) & 0xFFFF)
    write_u16(image, short_entry_offset + 26, file_cluster & 0xFFFF)
    write_u32(image, short_entry_offset + 28, len(content))
    update_fsinfo(image, bytes_per_sector, fsinfo_sector, fat_offset, cluster_count)

    os.makedirs(os.path.dirname(args.image), exist_ok=True)
    with open(args.image, "wb") as image_file:
        image_file.write(image)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())

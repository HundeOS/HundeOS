from dataclasses import dataclass
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

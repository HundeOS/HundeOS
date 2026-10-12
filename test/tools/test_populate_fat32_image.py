import struct

from populate_fat32_image import populate_image


SECTOR_SIZE = 512


def fat16_volume() -> bytearray:
    image = bytearray(12 * SECTOR_SIZE)
    image[0:3] = b"\xeb\x3c\x90"
    image[3:11] = b"mkfs.fat"
    struct.pack_into("<H", image, 11, SECTOR_SIZE)
    image[13] = 1
    struct.pack_into("<H", image, 14, 1)
    image[16] = 1
    struct.pack_into("<H", image, 17, 16)
    struct.pack_into("<H", image, 19, 12)
    image[21] = 0xF8
    struct.pack_into("<H", image, 22, 1)
    image[510:512] = b"\x55\xaa"

    fat_offset = SECTOR_SIZE
    for cluster, value in ((0, 0xFFF8), (1, 0xFFFF), (2, 0xFFFF), (3, 0xFFFF)):
        struct.pack_into("<H", image, fat_offset + cluster * 2, value)

    root_offset = 2 * SECTOR_SIZE
    image[root_offset:root_offset + 11] = b"FIP     BIN"
    image[root_offset + 11] = 0x20
    struct.pack_into("<H", image, root_offset + 26, 2)
    struct.pack_into("<I", image, root_offset + 28, 3)
    image[root_offset + 32:root_offset + 43] = b"BOOT    SD "
    image[root_offset + 43] = 0x20
    struct.pack_into("<H", image, root_offset + 32 + 26, 3)
    struct.pack_into("<I", image, root_offset + 32 + 28, 3)
    image[3 * SECTOR_SIZE:3 * SECTOR_SIZE + 3] = b"fip"
    image[4 * SECTOR_SIZE:4 * SECTOR_SIZE + 3] = b"old"
    return image


def test_populate_image_replaces_root_file_in_fat16_without_changing_firmware():
    image = fat16_volume()
    replacement = b"new boot image" * 50

    populate_image(image, "BOOT.SD", replacement)

    root_offset = 2 * SECTOR_SIZE
    boot_entry = root_offset + 32
    assert b"FIP     BIN" == image[root_offset:root_offset + 11]
    assert 2 == struct.unpack_from("<H", image, root_offset + 26)[0]
    assert b"fip" == image[3 * SECTOR_SIZE:3 * SECTOR_SIZE + 3]
    assert 4 == struct.unpack_from("<H", image, boot_entry + 26)[0]
    assert len(replacement) == struct.unpack_from("<I", image, boot_entry + 28)[0]
    assert 5 == struct.unpack_from("<H", image, SECTOR_SIZE + 4 * 2)[0]
    assert 0xFFFF == struct.unpack_from("<H", image, SECTOR_SIZE + 5 * 2)[0]
    assert replacement[:SECTOR_SIZE] == image[5 * SECTOR_SIZE:6 * SECTOR_SIZE]
    assert replacement[SECTOR_SIZE:] == image[
        6 * SECTOR_SIZE:6 * SECTOR_SIZE + len(replacement) - SECTOR_SIZE
    ]

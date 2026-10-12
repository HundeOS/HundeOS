import pytest

from build_milkv_duo_sd_image import (
    MBR_SIGNATURE,
    PARTITION_TABLE_OFFSET,
    Partition,
    SECTOR_SIZE,
    build_image,
    read_partition,
    write_partition,
)


def make_template(path, boot_sectors=4, root_sectors=8, root_index=1, root_start=None):
    if root_start is None:
        root_start = 1 + boot_sectors
    mbr = bytearray(SECTOR_SIZE)
    write_partition(mbr, 0, Partition(True, 0x0C, 1, boot_sectors))
    write_partition(mbr, root_index, Partition(False, 0x83, root_start, root_sectors))
    mbr[-2:] = MBR_SIGNATURE
    path.write_bytes(mbr + b"\0" * (root_start * SECTOR_SIZE - len(mbr)))


def test_build_image_preserves_boot_and_places_ext2_in_second_partition(tmp_path):
    template = tmp_path / "template.img"
    boot = tmp_path / "boot.fat"
    root = tmp_path / "root.ext2"
    output = tmp_path / "hundeos.img"
    make_template(template)
    boot.write_bytes(b"B" * (4 * SECTOR_SIZE))
    root.write_bytes(b"R" * (3 * SECTOR_SIZE))

    build_image(template, boot, root, output)

    image = output.read_bytes()
    first = read_partition(image[:SECTOR_SIZE], 0)
    second = read_partition(image[:SECTOR_SIZE], 1)
    assert Partition(True, 0x0C, 1, 4) == first
    assert Partition(False, 0x83, 5, 3) == second
    assert b"B" * (4 * SECTOR_SIZE) == image[SECTOR_SIZE:5 * SECTOR_SIZE]
    assert b"R" * (3 * SECTOR_SIZE) == image[5 * SECTOR_SIZE:]
    assert b"\0" * 32 == image[
        PARTITION_TABLE_OFFSET + 2 * 16:PARTITION_TABLE_OFFSET + 4 * 16
    ]


def test_build_image_preserves_duo_s_gap_and_places_ext2_in_third_partition(tmp_path):
    template = tmp_path / "template.img"
    boot = tmp_path / "boot.fat"
    root = tmp_path / "root.ext2"
    output = tmp_path / "hundeos.img"
    make_template(template, boot_sectors=4, root_index=2, root_start=9)
    template_data = bytearray(template.read_bytes())
    template_data[5 * SECTOR_SIZE:9 * SECTOR_SIZE] = b"L" * (4 * SECTOR_SIZE)
    template.write_bytes(template_data)
    boot.write_bytes(b"B" * (4 * SECTOR_SIZE))
    root.write_bytes(b"R" * (3 * SECTOR_SIZE))

    build_image(template, boot, root, output)

    image = output.read_bytes()
    first = read_partition(image[:SECTOR_SIZE], 0)
    third = read_partition(image[:SECTOR_SIZE], 2)
    assert Partition(True, 0x0C, 1, 4) == first
    assert Partition(False, 0x83, 9, 3) == third
    assert b"B" * (4 * SECTOR_SIZE) == image[SECTOR_SIZE:5 * SECTOR_SIZE]
    assert b"L" * (4 * SECTOR_SIZE) == image[5 * SECTOR_SIZE:9 * SECTOR_SIZE]
    assert b"R" * (3 * SECTOR_SIZE) == image[9 * SECTOR_SIZE:]


def test_build_image_adds_boot_config_after_duo_s_logo_and_moves_root(tmp_path):
    template = tmp_path / "template.img"
    boot = tmp_path / "boot.fat"
    boot_config = tmp_path / "boot-config.ext2"
    root = tmp_path / "root.ext2"
    output = tmp_path / "hundeos.img"
    make_template(template, boot_sectors=4, root_index=2, root_start=9)
    template_data = bytearray(template.read_bytes())
    template_data[5 * SECTOR_SIZE:9 * SECTOR_SIZE] = b"L" * (4 * SECTOR_SIZE)
    template.write_bytes(template_data)
    boot.write_bytes(b"B" * (4 * SECTOR_SIZE))
    boot_config.write_bytes(b"C" * (2 * SECTOR_SIZE))
    root.write_bytes(b"R" * (3 * SECTOR_SIZE))

    build_image(template, boot, root, output, boot_config)

    image = output.read_bytes()
    second = read_partition(image[:SECTOR_SIZE], 1)
    third = read_partition(image[:SECTOR_SIZE], 2)
    assert Partition(False, 0x83, 9, 2) == second
    assert Partition(False, 0x83, 11, 3) == third
    assert b"L" * (4 * SECTOR_SIZE) == image[5 * SECTOR_SIZE:9 * SECTOR_SIZE]
    assert b"C" * (2 * SECTOR_SIZE) == image[9 * SECTOR_SIZE:11 * SECTOR_SIZE]
    assert b"R" * (3 * SECTOR_SIZE) == image[11 * SECTOR_SIZE:]


def test_build_image_rejects_non_milkv_partition_layout(tmp_path):
    template = tmp_path / "template.img"
    boot = tmp_path / "boot.fat"
    root = tmp_path / "root.ext2"
    output = tmp_path / "hundeos.img"
    make_template(template)
    mbr = bytearray(template.read_bytes())
    write_partition(mbr, 0, Partition(False, 0x0C, 1, 4))
    template.write_bytes(mbr)
    boot.write_bytes(b"B" * (4 * SECTOR_SIZE))
    root.write_bytes(b"R" * SECTOR_SIZE)

    with pytest.raises(RuntimeError, match="Milk-V Duo boot partition layout"):
        build_image(template, boot, root, output)

    assert not output.exists()

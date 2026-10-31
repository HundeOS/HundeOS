import pytest

from build_pi_qemu_sd_image import CARD_SIZE, FIRST_PARTITION_SECTOR, build_image
from mbr import MBR_SIGNATURE, Partition, SECTOR_SIZE, read_partition


def test_build_image_places_boot_config_and_root_in_sd_partitions(tmp_path):
    boot = tmp_path / "boot.img"
    boot_config = tmp_path / "boot-config.img"
    root = tmp_path / "root.img"
    output = tmp_path / "card.img"
    boot.write_bytes(b"B" * (2 * SECTOR_SIZE))
    boot_config.write_bytes(b"C" * SECTOR_SIZE)
    root.write_bytes(b"R" * (3 * SECTOR_SIZE))

    build_image(boot, boot_config, root, output)

    with output.open("rb") as image:
        mbr = image.read(SECTOR_SIZE)
        assert mbr[-2:] == MBR_SIGNATURE
        assert read_partition(mbr, 0) == Partition(True, 0x0C, FIRST_PARTITION_SECTOR, 2)
        assert read_partition(mbr, 1) == Partition(False, 0x83, FIRST_PARTITION_SECTOR + 2, 1)
        assert read_partition(mbr, 2) == Partition(False, 0x83, FIRST_PARTITION_SECTOR + 3, 3)
        for start, expected in ((FIRST_PARTITION_SECTOR, b"B" * (2 * SECTOR_SIZE)),
                                (FIRST_PARTITION_SECTOR + 2, b"C" * SECTOR_SIZE),
                                (FIRST_PARTITION_SECTOR + 3, b"R" * (3 * SECTOR_SIZE))):
            image.seek(start * SECTOR_SIZE)
            assert image.read(len(expected)) == expected
    assert output.stat().st_size == CARD_SIZE


@pytest.mark.parametrize("invalid", [b"", b"B"])
def test_build_image_rejects_empty_or_unaligned_partition(tmp_path, invalid):
    boot = tmp_path / "boot.img"
    boot_config = tmp_path / "boot-config.img"
    root = tmp_path / "root.img"
    output = tmp_path / "card.img"
    boot.write_bytes(invalid)
    boot_config.write_bytes(b"C" * SECTOR_SIZE)
    root.write_bytes(b"R" * SECTOR_SIZE)

    with pytest.raises(RuntimeError, match="sector aligned"):
        build_image(boot, boot_config, root, output)
    assert not output.exists()

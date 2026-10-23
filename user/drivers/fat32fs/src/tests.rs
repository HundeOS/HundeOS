use super::*;
use alloc::collections::BTreeMap;

const FAT_START: u64 = 32;
const FAT_SIZE: u64 = 800;
const DATA_START: u64 = FAT_START + 2 * FAT_SIZE;
const TOTAL_SECTORS: u64 = 100_000;

#[derive(Clone)]
struct Disk {
    sectors: BTreeMap<u64, [u8; SECTOR_SIZE]>,
    count: u64,
    fail: Option<Sector>,
    writes: usize,
    write_failures: Vec<usize>,
}

fn put_word(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn put_double_word(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn directory_entry(name: &[u8; 11], attributes: u8, cluster: u32, size: u32) -> [u8; 32] {
    let mut entry = [0; 32];
    entry[..11].copy_from_slice(name);
    entry[11] = attributes;
    put_word(&mut entry, 20, (cluster >> 16) as u16);
    put_word(&mut entry, 26, cluster as u16);
    put_double_word(&mut entry, 28, size);
    entry
}

impl Disk {
    fn new(sectors_per_cluster: u8) -> Self {
        let count = TOTAL_SECTORS * u64::from(sectors_per_cluster);
        let mut boot = [0; SECTOR_SIZE];
        boot[..3].copy_from_slice(&[0xeb, 0x58, 0x90]);
        put_word(&mut boot, 11, SECTOR_SIZE as u16);
        boot[13] = sectors_per_cluster;
        put_word(&mut boot, 14, FAT_START as u16);
        boot[16] = 2;
        boot[21] = 0xf8;
        put_word(&mut boot, 48, 0xffff);
        put_word(&mut boot, 50, 0xffff);
        put_double_word(&mut boot, 32, count as u32);
        put_double_word(&mut boot, 36, FAT_SIZE as u32);
        put_double_word(&mut boot, 44, 2);
        boot[510..512].copy_from_slice(&[0x55, 0xaa]);
        let mut disk = Self {
            sectors: BTreeMap::new(),
            count,
            fail: None,
            writes: 0,
            write_failures: Vec::new(),
        };
        disk.sectors.insert(0, boot);
        for (cluster, next) in [
            (0, 0x0fff_fff8),
            (1, END_OF_CHAIN),
            (2, 6),
            (3, 7),
            (4, END_OF_CHAIN),
            (5, END_OF_CHAIN),
            (6, END_OF_CHAIN),
            (7, END_OF_CHAIN),
        ] {
            disk.fat(cluster, next);
        }
        let mut root = [0xe5; SECTOR_SIZE];
        root[..32].copy_from_slice(&directory_entry(
            b"HELLO   TXT",
            0x20,
            3,
            SECTOR_SIZE as u32 * u32::from(sectors_per_cluster) + 188,
        ));
        root[32..64].copy_from_slice(&directory_entry(b"DOCS       ", 0x10, 4, 0));
        root[64..96].copy_from_slice(&directory_entry(b"EMPTY   TXT", 0x20, 0, 0));
        root[96..128].copy_from_slice(&directory_entry(b"VOLUME     ", 0x8, 0, 0));
        root[128..160].copy_from_slice(&directory_entry(b"LONGNAME   ", 0xf, 0, 0));
        disk.sectors.insert(DATA_START, root);
        for offset in 1..sectors_per_cluster {
            disk.sectors
                .insert(DATA_START + u64::from(offset), [0xe5; SECTOR_SIZE]);
        }
        let mut tail = [0; SECTOR_SIZE];
        tail[..32].copy_from_slice(&directory_entry(b"TAIL    TXT", 0x20, 0, 0));
        disk.sectors.insert(disk.cluster_sector(6), tail);
        let mut docs = [0; SECTOR_SIZE];
        docs[..32].copy_from_slice(&directory_entry(b".          ", 0x10, 4, 0));
        docs[32..64].copy_from_slice(&directory_entry(b"..         ", 0x10, 0, 0));
        docs[64..96].copy_from_slice(&directory_entry(b"NOTE    TXT", 0x20, 5, 6));
        disk.sectors.insert(disk.cluster_sector(4), docs);
        let mut note = [0; SECTOR_SIZE];
        note[..6].copy_from_slice(b"nested");
        disk.sectors.insert(disk.cluster_sector(5), note);
        for offset in 0..sectors_per_cluster {
            disk.sectors.insert(
                disk.cluster_sector(3) + u64::from(offset),
                [b'A'; SECTOR_SIZE],
            );
        }
        disk.sectors
            .insert(disk.cluster_sector(7), [b'B'; SECTOR_SIZE]);
        disk
    }

    fn cluster_sector(&self, cluster: u32) -> u64 {
        DATA_START + u64::from(cluster - 2) * u64::from(self.sectors[&0][13])
    }

    fn fat(&mut self, cluster: u32, next: u32) {
        for start in [FAT_START, FAT_START + FAT_SIZE] {
            let offset = u64::from(cluster) * 4;
            let sector = self
                .sectors
                .entry(start + offset / SECTOR_SIZE as u64)
                .or_insert([0; SECTOR_SIZE]);
            put_double_word(sector, (offset % SECTOR_SIZE as u64) as usize, next);
        }
    }

    fn boot(&mut self) -> &mut [u8; SECTOR_SIZE] {
        self.sectors.get_mut(&0).expect("Boot sector")
    }
}

impl Device for Disk {
    fn sector_count(&self) -> u64 {
        self.count
    }

    fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), block::Error> {
        if sector.0 >= self.count || destination.len() != SECTOR_SIZE || self.fail == Some(sector) {
            return Err(block::Error::InvalidTransfer);
        }
        destination.copy_from_slice(self.sectors.get(&sector.0).unwrap_or(&[0; SECTOR_SIZE]));
        Ok(())
    }

    fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), block::Error> {
        if sector.0 >= self.count || source.len() != SECTOR_SIZE {
            return Err(block::Error::InvalidTransfer);
        }
        self.writes += 1;
        if source.iter().all(|byte| *byte == 0) {
            self.sectors.remove(&sector.0);
        } else {
            self.sectors.insert(sector.0, source.try_into().unwrap());
        }
        if self.write_failures.contains(&self.writes) {
            return Err(block::Error::InvalidTransfer);
        }
        Ok(())
    }
}

#[test]
fn resolves_nested_short_names_case_insensitively() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    let entry = volume.entry("/docs/note.txt").unwrap();
    assert_eq!(entry.name, "NOTE.TXT");
    assert_eq!(entry.kind, Kind::File);
    assert_eq!(entry.size, 6);
    let mut bytes = [0; 8];
    assert_eq!(volume.read("/DOCS/NOTE.TXT", 0, &mut bytes), Ok(6));
    assert_eq!(&bytes[..6], b"nested");
    assert_eq!(volume.entry("/").unwrap().kind, Kind::Directory);
}

#[test]
fn lists_across_directory_clusters_with_pagination_and_filtered_slots() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    let names: Vec<_> = volume
        .list("/", 0, 20)
        .unwrap()
        .into_iter()
        .map(|entry| entry.name)
        .collect();
    assert_eq!(names, ["HELLO.TXT", "DOCS", "EMPTY.TXT", "TAIL.TXT"]);
    assert_eq!(volume.list("/", 3, 1).unwrap()[0].name, "TAIL.TXT");
    assert!(volume.list("/", u64::MAX, 10).unwrap().is_empty());
    assert!(volume.list("/", 0, 0).unwrap().is_empty());
    assert_eq!(volume.list("/DOCS", 0, 10).unwrap().len(), 1);
}

#[test]
fn reads_fragmented_clusters_and_preserves_eof_buffer_tail() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    let mut bytes = [0; 20];
    assert_eq!(volume.read("/hello.txt", 502, &mut bytes), Ok(20));
    assert_eq!(&bytes[..10], &[b'A'; 10]);
    assert_eq!(&bytes[10..], &[b'B'; 10]);
    bytes.fill(0xcc);
    assert_eq!(volume.read("/HELLO.TXT", 697, &mut bytes), Ok(3));
    assert_eq!(&bytes[..3], b"BBB");
    assert_eq!(&bytes[3..], &[0xcc; 17]);
    assert_eq!(volume.read("/HELLO.TXT", 700, &mut bytes), Ok(0));
    assert_eq!(volume.read("/HELLO.TXT", u64::MAX, &mut bytes), Ok(0));
    assert_eq!(volume.read("/EMPTY.TXT", 0, &mut bytes), Ok(0));
    assert_eq!(volume.read("/HELLO.TXT", 0, &mut []), Ok(0));
}

#[test]
fn supports_multiple_sectors_per_cluster_and_independent_volumes() {
    let mut first = Volume::mount(Disk::new(2)).unwrap();
    let mut disk = Disk::new(1);
    disk.sectors
        .insert(disk.cluster_sector(3), [b'X'; SECTOR_SIZE]);
    let mut second = Volume::mount(disk).unwrap();
    let mut bytes = [0; 8];
    assert_eq!(first.read("/HELLO.TXT", 1020, &mut bytes), Ok(8));
    assert_eq!(&bytes, b"AAAABBBB");
    second.read("/HELLO.TXT", 0, &mut bytes).unwrap();
    assert_eq!(&bytes, b"XXXXXXXX");
    first.read("/HELLO.TXT", 0, &mut bytes).unwrap();
    assert_eq!(&bytes, b"AAAAAAAA");
}

#[test]
fn honors_active_fat_and_masks_reserved_fat_entry_bits() {
    let mut disk = Disk::new(1);
    put_word(disk.boot(), 40, 0x81);
    disk.fat(3, 0xa000_0007);
    disk.sectors.insert(FAT_START, [0; SECTOR_SIZE]);
    let mut volume = Volume::mount(disk).unwrap();
    let mut bytes = [0; 8];
    assert_eq!(volume.read("/HELLO.TXT", 508, &mut bytes), Ok(8));
    assert_eq!(&bytes, b"AAAABBBB");
}

#[test]
fn reads_high_cluster_words_and_fat_sectors_beyond_the_first() {
    let mut disk = Disk::new(1);
    let cluster = 65_540;
    disk.fat(cluster, 7);
    disk.sectors
        .insert(disk.cluster_sector(cluster), [b'Z'; SECTOR_SIZE]);
    disk.sectors.get_mut(&DATA_START).unwrap()[..32].copy_from_slice(&directory_entry(
        b"HELLO   TXT",
        0x20,
        cluster,
        700,
    ));
    let mut volume = Volume::mount(disk).unwrap();
    let mut bytes = [0; 8];
    assert_eq!(volume.read("/HELLO.TXT", 508, &mut bytes), Ok(8));
    assert_eq!(&bytes, b"ZZZZBBBB");
}

#[test]
fn rejects_bad_geometry_and_unsupported_formats() {
    for (offset, value) in [(13, 0), (13, 3), (16, 0), (510, 0)] {
        let mut disk = Disk::new(1);
        disk.boot()[offset] = value;
        assert_eq!(Volume::mount(disk).err(), Some(Error::Corrupt));
    }
    for (offset, value) in [(14, 0), (40, 0x82)] {
        let mut disk = Disk::new(1);
        put_word(disk.boot(), offset, value);
        assert_eq!(Volume::mount(disk).err(), Some(Error::Corrupt));
    }
    for (offset, value) in [(32, 1), (36, 1), (44, 0), (44, 100_000)] {
        let mut disk = Disk::new(1);
        put_double_word(disk.boot(), offset, value);
        assert_eq!(Volume::mount(disk).err(), Some(Error::Corrupt));
    }
    let mut disk = Disk::new(1);
    disk.count -= 1;
    assert_eq!(Volume::mount(disk).err(), Some(Error::Corrupt));
    for offset in [11, 17, 19, 22, 42] {
        let mut disk = Disk::new(1);
        put_word(disk.boot(), offset, 1);
        assert_eq!(Volume::mount(disk).err(), Some(Error::Unsupported));
    }
    let mut disk = Disk::new(1);
    put_double_word(disk.boot(), 32, 60_000);
    assert_eq!(Volume::mount(disk).err(), Some(Error::Unsupported));
    let mut disk = Disk::new(1);
    disk.boot()[16] = 3;
    assert_eq!(Volume::mount(disk).err(), Some(Error::Unsupported));
}

#[test]
fn rejects_free_reserved_bad_and_out_of_range_chain_links() {
    for next in [0, 1, 100_000, RESERVED_CLUSTER, 0x0fff_fff7] {
        let mut disk = Disk::new(1);
        disk.fat(3, next);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.read("/HELLO.TXT", 0, &mut [0; 700]),
            Err(Error::Corrupt)
        );
    }
}

#[test]
fn rejects_cyclic_and_truncated_file_and_directory_chains() {
    for next in [3, 7] {
        let mut disk = Disk::new(1);
        disk.fat(7, next);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.read("/HELLO.TXT", 0, &mut [0; 700]),
            Err(Error::Corrupt)
        );
    }
    let mut disk = Disk::new(1);
    disk.fat(3, END_OF_CHAIN);
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(
        volume.read("/HELLO.TXT", 0, &mut [0; 700]),
        Err(Error::Corrupt)
    );
    for next in [2, 6] {
        let mut disk = Disk::new(1);
        disk.fat(6, next);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.list("/", 0, 30), Err(Error::Corrupt));
    }
}

#[test]
fn propagates_boot_fat_and_data_device_failures() {
    for sector in [0, FAT_START, DATA_START, DATA_START + 1] {
        let mut disk = Disk::new(1);
        disk.fail = Some(Sector(sector));
        let result =
            Volume::mount(disk).and_then(|mut volume| volume.read("/HELLO.TXT", 0, &mut [0; 1]));
        assert_eq!(result, Err(Error::Device(block::Error::InvalidTransfer)));
    }
}

#[test]
fn rejects_invalid_paths_and_wrong_entry_kinds() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    for path in [
        "",
        "HELLO.TXT",
        "//DOCS",
        "/DOCS/",
        "/./DOCS",
        "/../DOCS",
        "/bad\0name",
    ] {
        assert_eq!(volume.entry(path), Err(Error::InvalidPath));
    }
    assert_eq!(volume.entry("/missing.txt"), Err(Error::NotFound));
    assert_eq!(volume.entry("/longlonglongname.txt"), Err(Error::NotFound));
    assert_eq!(volume.entry("/HELLO.TXT/FILE"), Err(Error::NotDirectory));
    assert_eq!(volume.list("/HELLO.TXT", 0, 1), Err(Error::NotDirectory));
    assert_eq!(volume.read("/DOCS", 0, &mut [0; 1]), Err(Error::NotFile));
}

#[test]
fn rejects_invalid_directory_entries_without_indexing_or_utf8_panics() {
    for (offset, value, error) in [
        (0, 0x80, Error::Unsupported),
        (0, b'/', Error::Corrupt),
        (11, 0x80, Error::Corrupt),
    ] {
        let mut disk = Disk::new(1);
        disk.sectors.get_mut(&DATA_START).unwrap()[offset] = value;
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.list("/", 0, 10), Err(error));
    }
    for cluster in [0, 1, 100_000] {
        let mut disk = Disk::new(1);
        let root = disk.sectors.get_mut(&DATA_START).unwrap();
        root[..32].copy_from_slice(&directory_entry(b"HELLO   TXT", 0x20, cluster, 700));
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.entry("/HELLO.TXT"), Err(Error::Corrupt));
    }
}

mod mutation;
mod service;

fn long_records(name: &str, alias: &[u8; 11]) -> Vec<[u8; 32]> {
    let units: Vec<_> = name.encode_utf16().collect();
    let count = units.len().div_ceil(13);
    let checksum = alias
        .iter()
        .fold(0u8, |sum, byte| sum.rotate_right(1).wrapping_add(*byte));
    (1..=count)
        .rev()
        .map(|ordinal| {
            let mut record = [0; 32];
            record[0] = ordinal as u8 | if ordinal == count { 0x40 } else { 0 };
            record[11] = 0xf;
            record[13] = checksum;
            for (index, offset) in [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
                .iter()
                .enumerate()
            {
                let position = (ordinal - 1) * 13 + index;
                let value = units
                    .get(position)
                    .copied()
                    .unwrap_or(if position == units.len() { 0 } else { 0xffff });
                put_word(&mut record, *offset, value);
            }
            record
        })
        .collect()
}

impl Disk {
    fn with_long_name(mut self, name: &str) -> Self {
        let alias = b"LONGFI~1TXT";
        let records = long_records(name, alias);
        let start = 16 - (records.len() - 1);
        for (index, record) in records.iter().enumerate() {
            let slot = start + index;
            let sector = if slot < 16 {
                DATA_START
            } else {
                self.cluster_sector(6)
            };
            self.sectors.get_mut(&sector).unwrap()[(slot % 16) * 32..(slot % 16 + 1) * 32]
                .copy_from_slice(record);
        }
        let sector = self.cluster_sector(6);
        let tail = self.sectors.get_mut(&sector).unwrap();
        tail[32..64].copy_from_slice(&directory_entry(alias, 0x20, 0, 0));
        tail[64] = 0;
        self
    }
}

#[test]
fn resolves_long_names_and_aliases_across_fragmented_directory_clusters() {
    for name in ["Long filename spanning cluster.txt", "Long café 😀.txt"] {
        let mut volume = Volume::mount(Disk::new(1).with_long_name(name)).unwrap();
        let path = alloc::format!("/{name}");
        let long = volume.entry(&path).unwrap();
        assert_eq!(long.name, name);
        assert_eq!(
            volume.entry("/LONGFI~1.TXT").unwrap().location,
            long.location
        );
        assert!(
            volume
                .list("/", 0, 30)
                .unwrap()
                .iter()
                .any(|entry| entry.name == name)
        );
        volume.write(&path, 0, b"long-name write").unwrap();
        let mut bytes = [0; 15];
        assert_eq!(volume.read("/LONGFI~1.TXT", 0, &mut bytes), Ok(15));
        assert_eq!(&bytes, b"long-name write");
        volume.remove_file(&path).unwrap();
        assert_eq!(volume.entry("/LONGFI~1.TXT"), Err(Error::NotFound));
    }
}

#[test]
fn malformed_long_name_sequence_falls_back_to_short_alias() {
    let mut disk = Disk::new(1).with_long_name("Long filename spanning cluster.txt");
    let sector = disk.cluster_sector(6);
    disk.sectors.get_mut(&sector).unwrap()[13] ^= 1;
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(
        volume.entry("/Long filename spanning cluster.txt"),
        Err(Error::NotFound)
    );
    assert_eq!(volume.entry("/LONGFI~1.TXT").unwrap().name, "LONGFI~1.TXT");
}

#[test]
fn long_names_with_invalid_utf16_or_oversized_protocol_names_keep_aliases() {
    let mut disk = Disk::new(1).with_long_name("Long filename spanning cluster.txt");
    let sector = disk.cluster_sector(6);
    put_word(disk.sectors.get_mut(&sector).unwrap(), 1, 0xd800);
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.entry("/LONGFI~1.TXT").unwrap().name, "LONGFI~1.TXT");
    let name = "界".repeat(90);
    let mut volume = Volume::mount(Disk::new(1).with_long_name(&name)).unwrap();
    assert_eq!(volume.entry("/LONGFI~1.TXT").unwrap().name, "LONGFI~1.TXT");
}

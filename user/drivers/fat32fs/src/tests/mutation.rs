use super::*;

impl Disk {
    fn with_fsinfo(mut self) -> Self {
        put_word(self.boot(), 48, 1);
        put_word(self.boot(), 50, 6);
        let mut info = [0; SECTOR_SIZE];
        for (offset, value) in [
            (0, 0x4161_5252),
            (484, 0x6141_7272),
            (488, 100),
            (492, 8),
            (508, 0xaa55_0000),
        ] {
            put_double_word(&mut info, offset, value);
        }
        self.sectors.insert(1, info);
        self.sectors.insert(7, info);
        self
    }
}

fn fat_value(disk: &Disk, fat: u64, cluster: u32) -> u32 {
    let offset = u64::from(cluster) * 4;
    double_word(
        &disk.sectors[&(fat + offset / SECTOR_SIZE as u64)],
        (offset % SECTOR_SIZE as u64) as usize,
    )
}

#[test]
fn reading_does_not_write_to_the_device() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    volume.list("/", 0, 30).unwrap();
    volume.read("/HELLO.TXT", 0, &mut [0; 700]).unwrap();
    assert_eq!(volume.device.writes, 0);
}

#[test]
fn creates_and_grows_files_with_zero_filled_gaps_and_remounts() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    volume.create_file("/new.txt").unwrap();
    assert_eq!(volume.entry("/NEW.TXT").unwrap().size, 0);
    assert_eq!(volume.create_file("/NeW.TxT"), Err(Error::AlreadyExists));
    volume.write("/new.txt", 1100, &[9; 600]).unwrap();
    let mut volume = Volume::mount(volume.device).unwrap();
    let mut bytes = [0xff; 1707];
    assert_eq!(volume.read("/new.txt", 0, &mut bytes), Ok(1700));
    assert_eq!(&bytes[..1100], &[0; 1100]);
    assert_eq!(&bytes[1100..1700], &[9; 600]);
    assert_eq!(&bytes[1700..], &[0xff; 7]);
    assert_eq!(volume.entry("/HELLO.TXT").unwrap().size, 700);
    assert_eq!(
        volume.device.sectors[&FAT_START],
        volume.device.sectors[&(FAT_START + FAT_SIZE)]
    );
}

#[test]
fn overwrites_preserve_file_size_and_unwritten_bytes() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    volume.write("/HELLO.TXT", 510, b"edit").unwrap();
    let mut bytes = [0; 8];
    volume.read("/HELLO.TXT", 508, &mut bytes).unwrap();
    assert_eq!(&bytes, b"AAeditBB");
    assert_eq!(volume.entry("/HELLO.TXT").unwrap().size, 700);
    let before = volume.device.writes;
    assert_eq!(volume.write("/HELLO.TXT", u64::MAX, &[]), Ok(0));
    assert_eq!(
        volume.write("/HELLO.TXT", u64::MAX, b"x"),
        Err(Error::FileTooLarge)
    );
    assert_eq!(
        volume.write("/HELLO.TXT", u64::from(u32::MAX), b"x"),
        Err(Error::FileTooLarge)
    );
    assert_eq!(volume.device.writes, before);
}

#[test]
fn growth_preserves_old_data_and_zeroes_existing_cluster_tails() {
    for sectors_per_cluster in [1, 2, 8] {
        let mut volume = Volume::mount(Disk::new(sectors_per_cluster)).unwrap();
        let size = volume.entry("/HELLO.TXT").unwrap().size as usize;
        volume
            .write("/HELLO.TXT", size as u64 + 700, &[9; 600])
            .unwrap();
        let mut bytes = alloc::vec![0xff; size + 1300];
        assert_eq!(volume.read("/HELLO.TXT", 0, &mut bytes), Ok(size + 1300));
        let first_cluster = usize::from(sectors_per_cluster) * SECTOR_SIZE;
        assert!(bytes[..first_cluster].iter().all(|byte| *byte == b'A'));
        assert!(bytes[first_cluster..size].iter().all(|byte| *byte == b'B'));
        assert!(bytes[size..size + 700].iter().all(|byte| *byte == 0));
        assert_eq!(&bytes[size + 700..], &[9; 600]);
    }
}

#[test]
fn truncation_frees_clusters_preserves_identity_and_zeroes_reused_storage() {
    let mut volume = Volume::mount(Disk::new(1)).unwrap();
    let slot = volume.entry("/HELLO.TXT").unwrap().location;
    volume.truncate("/HELLO.TXT").unwrap();
    let entry = volume.entry("/HELLO.TXT").unwrap();
    assert_eq!(entry.location, slot);
    assert_eq!(entry.cluster, Cluster(0));
    assert_eq!(entry.size, 0);
    for cluster in [3, 7] {
        assert_eq!(
            fat_value(&volume.device, FAT_START, cluster) & CLUSTER_MASK,
            0
        );
    }
    volume.write("/HELLO.TXT", 600, b"x").unwrap();
    let mut bytes = [0xff; 601];
    volume.read("/HELLO.TXT", 0, &mut bytes).unwrap();
    assert_eq!(&bytes[..600], &[0; 600]);
    assert_eq!(bytes[600], b'x');
    volume.remove_file("/HELLO.TXT").unwrap();
    assert_eq!(volume.entry("/HELLO.TXT"), Err(Error::NotFound));
}

#[test]
fn creates_nested_directories_and_only_removes_empty_directories() {
    let mut volume = Volume::mount(Disk::new(2)).unwrap();
    volume.create_directory("/NEW").unwrap();
    volume.create_directory("/NEW/SUB").unwrap();
    let parent = volume.entry("/NEW").unwrap().cluster;
    let child = volume.entry("/NEW/SUB").unwrap().cluster;
    let bytes = &volume.device.sectors[&volume.geometry.sector(child).unwrap()];
    assert_eq!(word(bytes, 32 + 26), parent.0 as u16);
    let bytes = &volume.device.sectors[&volume.geometry.sector(parent).unwrap()];
    assert_eq!(word(bytes, 32 + 26), 0);
    assert_eq!(volume.list("/NEW", 0, 30).unwrap().len(), 1);
    assert_eq!(volume.remove_directory("/NEW"), Err(Error::NotEmpty));
    volume.remove_directory("/NEW/SUB").unwrap();
    volume.remove_directory("/NEW").unwrap();
    assert_eq!(volume.entry("/NEW"), Err(Error::NotFound));
    assert_eq!(volume.remove_directory("/"), Err(Error::AccessDenied));
}

fn fill_directory(disk: &mut Disk, cluster: u32, first_index: usize) {
    let mut sector = [0; SECTOR_SIZE];
    for index in 0..16 {
        let name = alloc::format!("F{:07}TXT", first_index + index);
        sector[index * 32..index * 32 + 32].copy_from_slice(&directory_entry(
            name.as_bytes().try_into().unwrap(),
            0x20,
            0,
            0,
        ));
    }
    disk.sectors.insert(disk.cluster_sector(cluster), sector);
}

#[test]
fn extends_full_directories_and_preserves_end_markers() {
    let mut disk = Disk::new(1);
    fill_directory(&mut disk, 2, 0);
    fill_directory(&mut disk, 6, 16);
    let mut volume = Volume::mount(disk).unwrap();
    volume.create_file("/NEW.TXT").unwrap();
    assert_eq!(volume.list("/", 0, 100).unwrap().len(), 33);
    assert_ne!(
        fat_value(&volume.device, FAT_START, 6) & CLUSTER_MASK,
        END_OF_CHAIN
    );
    let mut disk = Disk::new(1);
    fill_directory(&mut disk, 2, 0);
    let tail = disk.cluster_sector(6);
    disk.sectors.get_mut(&tail).unwrap()[64..96].copy_from_slice(&directory_entry(
        b"GHOST   TXT",
        0x20,
        0,
        0,
    ));
    let mut volume = Volume::mount(disk).unwrap();
    volume.create_file("/NEW.TXT").unwrap();
    assert_eq!(volume.list("/", 0, 100).unwrap().len(), 18);
    assert_eq!(volume.entry("/GHOST.TXT"), Err(Error::NotFound));
}

#[test]
fn allocation_exhaustion_and_invalid_mutations_leave_the_image_unchanged() {
    let mut disk = Disk::new(1);
    for fat in [FAT_START, FAT_START + FAT_SIZE] {
        for sector in fat..fat + FAT_SIZE {
            disk.sectors.insert(sector, [0xff; SECTOR_SIZE]);
        }
    }
    for (cluster, next) in [
        (2, 6),
        (3, 7),
        (4, END_OF_CHAIN),
        (5, END_OF_CHAIN),
        (6, END_OF_CHAIN),
        (7, END_OF_CHAIN),
    ] {
        disk.fat(cluster, next);
    }
    let original = disk.sectors.clone();
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.write("/HELLO.TXT", 1024, b"x"), Err(Error::NoSpace));
    for path in [
        "/",
        "/TOOLONGNAME.TXT",
        "/A.B.C",
        "/NAME.",
        "/HAS SPACE",
        "/BAD?NAME",
    ] {
        assert_eq!(volume.create_file(path), Err(Error::InvalidPath));
    }
    assert_eq!(volume.create_file("/MISSING/NEW"), Err(Error::NotFound));
    assert_eq!(volume.write("/DOCS", 0, b"x"), Err(Error::NotFile));
    assert_eq!(volume.remove_file("/DOCS"), Err(Error::NotFile));
    assert_eq!(
        volume.remove_directory("/HELLO.TXT"),
        Err(Error::NotDirectory)
    );
    assert_eq!(volume.device.writes, 0);
    assert_eq!(volume.device.sectors, original);
}

#[test]
fn respects_read_only_attributes_and_rejects_parent_chain_aliases() {
    let mut disk = Disk::new(1);
    disk.sectors.get_mut(&DATA_START).unwrap()[11] |= 1;
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(
        volume.write("/HELLO.TXT", 0, b"x"),
        Err(Error::AccessDenied)
    );
    assert_eq!(volume.truncate("/HELLO.TXT"), Err(Error::AccessDenied));
    assert_eq!(volume.remove_file("/HELLO.TXT"), Err(Error::AccessDenied));
    let mut disk = Disk::new(1);
    disk.sectors.get_mut(&DATA_START).unwrap()[..32].copy_from_slice(&directory_entry(
        b"HELLO   TXT",
        0x20,
        2,
        700,
    ));
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.write("/HELLO.TXT", 0, b"x"), Err(Error::Corrupt));
    assert_eq!(volume.truncate("/HELLO.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.remove_file("/HELLO.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.device.writes, 0);
}

#[test]
fn mirrors_fat_updates_preserves_high_bits_and_honors_active_fat() {
    let mut disk = Disk::new(1);
    disk.fat(3, 0xa000_0007);
    put_double_word(
        disk.sectors.get_mut(&(FAT_START + FAT_SIZE)).unwrap(),
        12,
        0xb000_0007,
    );
    let mut volume = Volume::mount(disk).unwrap();
    volume.truncate("/HELLO.TXT").unwrap();
    assert_eq!(fat_value(&volume.device, FAT_START, 3), 0xa000_0000);
    assert_eq!(
        fat_value(&volume.device, FAT_START + FAT_SIZE, 3),
        0xb000_0000
    );
    let mut disk = Disk::new(1);
    put_word(disk.boot(), 40, 0x81);
    disk.sectors.insert(FAT_START, [0x11; SECTOR_SIZE]);
    let first_fat = disk.sectors[&FAT_START];
    let mut volume = Volume::mount(disk).unwrap();
    volume.truncate("/HELLO.TXT").unwrap();
    assert_eq!(volume.device.sectors[&FAT_START], first_fat);
    assert_eq!(fat_value(&volume.device, FAT_START + FAT_SIZE, 3), 0);
}

#[test]
fn updates_primary_and_backup_fsinfo_counts_after_allocation_and_free() {
    let mut disk = Disk::new(1);
    put_word(disk.boot(), 48, 1);
    put_word(disk.boot(), 50, 6);
    let mut info = [0; SECTOR_SIZE];
    for (offset, value) in [
        (0, 0x4161_5252),
        (484, 0x6141_7272),
        (488, 100),
        (492, 8),
        (508, 0xaa55_0000),
    ] {
        put_double_word(&mut info, offset, value);
    }
    disk.sectors.insert(1, info);
    disk.sectors.insert(7, info);
    let mut volume = Volume::mount(disk).unwrap();
    volume.write("/HELLO.TXT", 1200, b"x").unwrap();
    for sector in [1, 7] {
        assert_eq!(double_word(&volume.device.sectors[&sector], 488), 99);
        assert_eq!(double_word(&volume.device.sectors[&sector], 492), u32::MAX);
    }
    volume.truncate("/HELLO.TXT").unwrap();
    for sector in [1, 7] {
        assert_eq!(double_word(&volume.device.sectors[&sector], 488), 102);
    }
}

#[test]
fn irrelevant_fsinfo_failures_do_not_block_nonallocating_writes() {
    let mut disk = Disk::new(1);
    put_word(disk.boot(), 48, 1);
    disk.fail = Some(Sector(1));
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.write("/HELLO.TXT", 0, b"x"), Ok(1));
    let original = volume.device.sectors.clone();
    let writes = volume.device.writes;
    assert_eq!(
        volume.write("/HELLO.TXT", 1200, b"x"),
        Err(Error::Device(block::Error::InvalidTransfer))
    );
    assert_eq!(volume.device.sectors, original);
    assert_eq!(volume.device.writes, writes);
}

#[test]
fn malformed_fat_mirrors_and_short_file_chains_are_not_mutated() {
    let mut disk = Disk::new(1);
    put_double_word(
        disk.sectors.get_mut(&(FAT_START + FAT_SIZE)).unwrap(),
        12,
        END_OF_CHAIN,
    );
    let original = disk.sectors.clone();
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.truncate("/HELLO.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.device.sectors, original);
    assert_eq!(volume.device.writes, 0);
    let mut disk = Disk::new(1);
    disk.fat(3, END_OF_CHAIN);
    let original = disk.sectors.clone();
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.truncate("/HELLO.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.remove_file("/HELLO.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.device.sectors, original);
    assert_eq!(volume.device.writes, 0);
}

#[test]
fn removes_associated_long_name_slots_without_affecting_neighbors() {
    let mut disk = Disk::new(1);
    let alias = directory_entry(b"LONGNA~1TXT", 0x20, 0, 0);
    let checksum = alias[..11]
        .iter()
        .fold(0u8, |sum, byte| sum.rotate_right(1).wrapping_add(*byte));
    let mut long = [0; 32];
    long[0] = 0x41;
    long[11] = 0xf;
    long[13] = checksum;
    let root = disk.sectors.get_mut(&DATA_START).unwrap();
    root[96..128].copy_from_slice(&long);
    root[128..160].copy_from_slice(&alias);
    let mut volume = Volume::mount(disk).unwrap();
    volume.remove_file("/LONGNA~1.TXT").unwrap();
    assert_eq!(volume.device.sectors[&DATA_START][96], 0xe5);
    assert_eq!(volume.device.sectors[&DATA_START][128], 0xe5);
    assert!(volume.entry("/HELLO.TXT").is_ok());
}

#[test]
fn reported_partial_write_failures_restore_every_touched_sector() {
    type Edit = fn(&mut Volume<Disk>) -> Result<(), Error>;
    let operations: [Edit; 5] = [
        |volume| volume.write("/HELLO.TXT", 1300, &[9; 700]).map(|_| ()),
        |volume| volume.create_file("/NEW.TXT"),
        |volume| volume.create_directory("/NEW"),
        |volume| volume.truncate("/HELLO.TXT"),
        |volume| volume.remove_file("/HELLO.TXT"),
    ];
    for edit in operations {
        let mut success = Volume::mount(Disk::new(1).with_fsinfo()).unwrap();
        edit(&mut success).unwrap();
        for failure in 1..=success.device.writes {
            let mut disk = Disk::new(1).with_fsinfo();
            let original = disk.sectors.clone();
            disk.write_failures.push(failure);
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                edit(&mut volume),
                Err(Error::Device(block::Error::InvalidTransfer))
            );
            assert_eq!(volume.device.sectors, original, "failed write {failure}");
            assert_eq!(volume.entry("/HELLO.TXT").unwrap().size, 700);
            assert!(!volume.recovery_required);
            edit(&mut volume).unwrap();
        }
    }
}

#[test]
fn corrupt_lfn_checksums_are_rejected_before_deletion() {
    let mut disk = Disk::new(1);
    let root = disk.sectors.get_mut(&DATA_START).unwrap();
    let mut long = [0; 32];
    long[0] = 0x41;
    long[11] = 0xf;
    long[13] = 0xff;
    root[96..128].copy_from_slice(&long);
    root[128..160].copy_from_slice(&directory_entry(b"LONGNA~1TXT", 0x20, 0, 0));
    let original = disk.sectors.clone();
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(volume.remove_file("/LONGNA~1.TXT"), Err(Error::Corrupt));
    assert_eq!(volume.device.sectors, original);
    assert_eq!(volume.device.writes, 0);
}

#[test]
fn unknown_or_invalid_fsinfo_counts_remain_unknown() {
    for count in [u32::MAX, TOTAL_SECTORS as u32, 0] {
        let mut disk = Disk::new(1).with_fsinfo();
        put_double_word(disk.sectors.get_mut(&1).unwrap(), 488, count);
        let mut volume = Volume::mount(disk).unwrap();
        volume.write("/HELLO.TXT", 1200, b"x").unwrap();
        assert_eq!(double_word(&volume.device.sectors[&1], 488), u32::MAX);
    }
}

#[test]
fn failed_rollback_quarantines_the_volume() {
    let mut disk = Disk::new(1);
    disk.write_failures.extend([2, 3]);
    let mut volume = Volume::mount(disk).unwrap();
    assert_eq!(
        volume.write("/HELLO.TXT", 1300, &[9; 700]),
        Err(Error::RecoveryRequired)
    );
    let writes = volume.device.writes;
    assert_eq!(volume.entry("/HELLO.TXT"), Err(Error::RecoveryRequired));
    assert_eq!(
        volume.read("/HELLO.TXT", 0, &mut [0; 1]),
        Err(Error::RecoveryRequired)
    );
    assert_eq!(volume.create_file("/NEW.TXT"), Err(Error::RecoveryRequired));
    assert_eq!(volume.truncate("/HELLO.TXT"), Err(Error::RecoveryRequired));
    assert_eq!(
        volume.remove_file("/HELLO.TXT"),
        Err(Error::RecoveryRequired)
    );
    assert_eq!(volume.device.writes, writes);
}

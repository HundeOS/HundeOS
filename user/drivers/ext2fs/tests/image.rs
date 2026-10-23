use hunde::block::{Device, Error, SECTOR_SIZE, Sector};
use hunde::inheritance::Owner;
use hunde::io::{
    Credentials, GroupId, Handle, Operation, ProcessId, Reply, Request, SessionId, UserId,
};
use hunde_ext2fs::{Access, Ext2, InodeNumber, Volume};
use std::fs::{self, File};
use std::io::{Read, Seek, SeekFrom, Write};
use std::path::PathBuf;
use std::process::Command;
use std::sync::atomic::{AtomicU64, Ordering};

static NEXT_IMAGE: AtomicU64 = AtomicU64::new(0);

struct Image {
    directory: PathBuf,
}

impl Image {
    fn new() -> Self {
        let directory = std::env::temp_dir().join(format!(
            "hunde-ext2-test-{}-{}",
            std::process::id(),
            NEXT_IMAGE.fetch_add(1, Ordering::Relaxed)
        ));
        fs::create_dir(&directory).unwrap();
        Self { directory }
    }
}

impl Drop for Image {
    fn drop(&mut self) {
        fs::remove_dir_all(&self.directory).unwrap();
    }
}

struct FileDevice {
    file: File,
    sectors: u64,
}

impl FileDevice {
    fn new(file: File) -> Self {
        let sectors = file.metadata().unwrap().len() / SECTOR_SIZE as u64;
        Self { file, sectors }
    }
}

impl Device for FileDevice {
    fn sector_count(&self) -> u64 {
        self.sectors
    }
    fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), Error> {
        self.file
            .seek(SeekFrom::Start(sector.0 * SECTOR_SIZE as u64))
            .unwrap();
        self.file.read_exact(destination).unwrap();
        Ok(())
    }
    fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), Error> {
        self.file
            .seek(SeekFrom::Start(sector.0 * SECTOR_SIZE as u64))
            .unwrap();
        self.file.write_all(source).unwrap();
        Ok(())
    }
}

fn inode_offset(image: &[u8], number: InodeNumber) -> usize {
    let inodes_per_group = u32::from_le_bytes(image[1064..1068].try_into().unwrap());
    let revision = u32::from_le_bytes(image[1100..1104].try_into().unwrap());
    let size = if revision == 0 {
        128
    } else {
        u16::from_le_bytes(image[1112..1114].try_into().unwrap()) as usize
    };
    let group = (number.0 - 1) / inodes_per_group;
    let descriptor = 2048 + group as usize * 32;
    let table = u32::from_le_bytes(image[descriptor + 8..descriptor + 12].try_into().unwrap());
    table as usize * 1024 + ((number.0 - 1) % inodes_per_group) as usize * size
}

#[test]
fn mutates_reclaims_and_remounts_a_real_ext2_image() {
    let image = Image::new();
    let root = image.directory.join("root");
    fs::create_dir(&root).unwrap();
    fs::create_dir(root.join("nested")).unwrap();
    fs::write(root.join("nested/small"), b"EXT2 from Rust\n").unwrap();
    let content: Vec<u8> = (0..320 * 1024).map(|index| (index % 251) as u8).collect();
    fs::write(root.join("large"), &content).unwrap();
    let path = image.directory.join("disk.img");
    let generated = Command::new("genext2fs")
        .args(["-b", "1024", "-N", "64", "-d"])
        .arg(&root)
        .arg(&path)
        .output();
    match generated {
        Ok(output) => assert!(
            output.status.success(),
            "{}",
            String::from_utf8_lossy(&output.stderr)
        ),
        Err(error) if error.kind() == std::io::ErrorKind::NotFound => {
            let output = Command::new("mke2fs")
                .args([
                    "-F",
                    "-q",
                    "-t",
                    "ext2",
                    "-b",
                    "1024",
                    "-I",
                    "128",
                    "-N",
                    "64",
                    "-O",
                    "none,filetype,sparse_super,large_file",
                    "-d",
                ])
                .arg(&root)
                .arg(&path)
                .arg("1024")
                .output()
                .expect("EXT2 image tests require genext2fs or mke2fs on PATH");
            assert!(
                output.status.success(),
                "{}",
                String::from_utf8_lossy(&output.stderr)
            );
        }
        Err(error) => panic!("Cannot run EXT2 image builder: {error}"),
    }
    let before = fs::read(&path).unwrap();
    let original_free = u32::from_le_bytes(before[1036..1040].try_into().unwrap());
    let original_free_inodes = u32::from_le_bytes(before[1040..1044].try_into().unwrap());
    let original_directories = u16::from_le_bytes(before[2064..2066].try_into().unwrap());
    let file = File::options().read(true).write(true).open(&path).unwrap();
    let mut volume = Volume::mount(FileDevice::new(file)).unwrap();
    let entries = volume.entries(InodeNumber(2)).unwrap();
    assert!(entries.iter().any(|entry| entry.name == b"nested"));
    let small = volume.lookup(b"/nested/small").unwrap();
    let mut data = [0; 64];
    let count = volume.read(small.number, 0, &mut data).unwrap();
    assert_eq!(&data[..count], b"EXT2 from Rust\n");
    let large = volume.lookup(b"/large").unwrap();
    assert_eq!(large.size, content.len() as u64);
    let mut observed = vec![0; content.len()];
    assert_eq!(
        volume.read(large.number, 0, &mut observed).unwrap(),
        observed.len()
    );
    assert_eq!(observed, content);
    let mut expected = content;
    for offset in [11 * 1024 + 1000, 267 * 1024 + 1000] {
        let patch = vec![99; 4096];
        assert_eq!(
            volume.write(large.number, offset as u64, &patch),
            Ok(patch.len())
        );
        expected[offset..offset + patch.len()].copy_from_slice(&patch);
    }
    let append_offset = 330 * 1024 + 11;
    assert_eq!(
        volume.write(large.number, append_offset as u64, b"tail"),
        Ok(4)
    );
    expected.resize(append_offset + 4, 0);
    expected[append_offset..].copy_from_slice(b"tail");
    let small_offset = (12 + 256) * 1024 + 17;
    assert_eq!(
        volume.write(small.number, small_offset as u64, b"end"),
        Ok(3)
    );
    let nested = volume.lookup(b"/nested").unwrap();
    let directory = volume
        .mkdir(nested.number, b"created", 0o750, 17, 19)
        .unwrap();
    let created = volume
        .create_file(directory.number, b"payload", 0o640, 17, 19)
        .unwrap();
    assert_eq!(volume.write(created.number, 11, b"persisted"), Ok(9));
    let empty = volume
        .create_file(InodeNumber(2), b"empty", 0o600, 0, 0)
        .unwrap();
    volume
        .link(nested.number, b"alias", created.number)
        .unwrap();
    drop(volume);
    let file = File::options().read(true).write(true).open(&path).unwrap();
    let mut volume = Volume::mount(FileDevice::new(file)).unwrap();
    let remounted = volume.lookup(b"/large").unwrap();
    assert_eq!(remounted.size, expected.len() as u64);
    let mut observed = vec![0; expected.len()];
    assert_eq!(
        volume.read(remounted.number, 0, &mut observed),
        Ok(expected.len())
    );
    assert_eq!(observed, expected);
    let small = volume.lookup(b"/nested/small").unwrap();
    let mut observed = vec![255; small_offset + 3];
    assert_eq!(
        volume.read(small.number, 0, &mut observed),
        Ok(observed.len())
    );
    assert_eq!(&observed[..15], b"EXT2 from Rust\n");
    assert!(observed[15..small_offset].iter().all(|byte| *byte == 0));
    assert_eq!(&observed[small_offset..], b"end");
    assert_eq!(volume.lookup(b"/nested/created"), Ok(directory.clone()));
    assert_eq!(
        volume.lookup(b"/nested/created/..").unwrap().number,
        nested.number
    );
    assert_eq!(volume.inode(nested.number).unwrap().links, nested.links + 1);
    assert_eq!(volume.lookup(b"/empty"), Ok(empty.clone()));
    let created = volume.lookup(b"/nested/created/payload").unwrap();
    assert_eq!(
        (created.mode, created.user, created.group, created.links),
        (0x8000 | 0o640, 17, 19, 2)
    );
    assert_eq!(volume.lookup(b"/nested/alias"), Ok(created.clone()));
    let mut observed = [255; 20];
    assert_eq!(volume.read(created.number, 0, &mut observed), Ok(20));
    assert_eq!(&observed[..11], &[0; 11]);
    assert_eq!(&observed[11..], b"persisted");
    let after = fs::read(&path).unwrap();
    let record = inode_offset(&after, created.number);
    assert_eq!(
        u16::from_le_bytes(after[record + 26..record + 28].try_into().unwrap()),
        2
    );
    assert_eq!(
        u32::from_le_bytes(after[1036..1040].try_into().unwrap()),
        original_free - 6
    );
    assert_eq!(
        u32::from_le_bytes(after[1040..1044].try_into().unwrap()),
        original_free_inodes - 3
    );
    assert_eq!(
        u16::from_le_bytes(after[2064..2066].try_into().unwrap()),
        original_directories + 1
    );
    let removed = [
        large.number,
        small.number,
        created.number,
        directory.number,
        empty.number,
    ];
    let released: u32 = removed
        .iter()
        .map(|number| {
            let offset = inode_offset(&after, *number) + 28;
            u32::from_le_bytes(after[offset..offset + 4].try_into().unwrap()) / 2
        })
        .sum();
    let free_before_removal = u32::from_le_bytes(after[1036..1040].try_into().unwrap());
    assert_eq!(
        volume.rmdir(nested.number, b"created"),
        Err(hunde_ext2fs::Error::NotEmpty)
    );
    volume.unlink(directory.number, b"payload").unwrap();
    volume.rmdir(nested.number, b"created").unwrap();
    drop(volume);
    let file = File::options().read(true).write(true).open(&path).unwrap();
    let mut volume = Volume::mount(FileDevice::new(file)).unwrap();
    let alias = volume.lookup(b"/nested/alias").unwrap();
    assert_eq!(alias.number, created.number);
    assert_eq!(alias.links, 1);
    let mut data = [255; 20];
    assert_eq!(volume.read(alias.number, 0, &mut data), Ok(20));
    assert_eq!(&data[11..], b"persisted");
    volume.unlink(nested.number, b"alias").unwrap();
    volume.unlink(InodeNumber(2), b"empty").unwrap();
    let owner = Owner::new(SessionId(7), ProcessId(17));
    let opened = volume
        .open_file(owner, b"/large", Access::ReadWrite)
        .unwrap();
    let another = volume
        .open_file(owner, b"/large", Access::ReadOnly)
        .unwrap();
    let child = Owner::new(SessionId(8), ProcessId(18));
    let Reply::Inheritance { token, flags } = volume.prepare_inherit(owner, opened).unwrap() else {
        panic!("Expected inheritance grant");
    };
    volume.commit_inherit(owner, token, child.pid).unwrap();
    volume.unlink(InodeNumber(2), b"large").unwrap();
    assert_eq!(volume.lookup(b"/large"), Err(hunde_ext2fs::Error::NotFound));
    assert_eq!(volume.inode(large.number).unwrap().links, 0);
    assert_eq!(volume.write_file(owner, opened, b"held"), Ok(4));
    let mut retained = [0; 4];
    assert_eq!(volume.read_file(owner, another, &mut retained), Ok(4));
    assert_eq!(&retained, b"held");
    volume.close_file(owner, opened).unwrap();
    assert_eq!(volume.inode(large.number).unwrap().links, 0);
    volume.unlink(nested.number, b"small").unwrap();
    let held = fs::read(&path).unwrap();
    let free_while_open = u32::from_le_bytes(held[1036..1040].try_into().unwrap());
    let large_record = inode_offset(&held, large.number);
    let retained_blocks = u32::from_le_bytes(
        held[large_record + 28..large_record + 32]
            .try_into()
            .unwrap(),
    ) / 2;
    assert!(retained_blocks > 0);
    volume.close_file(owner, another).unwrap();
    assert_eq!(volume.inode(large.number).unwrap().links, 0);
    assert_eq!(volume.claim_inherit(child, token, flags), Ok(opened));
    let mut inherited = [0; 4];
    assert_eq!(volume.read_file(child, opened, &mut inherited), Ok(4));
    assert_eq!(&inherited, &expected[4..8]);
    volume.close_file(child, opened).unwrap();
    drop(volume);
    let mut volume = Volume::mount(FileDevice::new(File::open(&path).unwrap())).unwrap();
    for deleted in [
        b"/large".as_slice(),
        b"/empty",
        b"/nested/small",
        b"/nested/created",
        b"/nested/alias",
    ] {
        assert_eq!(volume.lookup(deleted), Err(hunde_ext2fs::Error::NotFound));
    }
    assert_eq!(volume.entries(nested.number).unwrap().len(), 2);
    assert_eq!(volume.inode(nested.number).unwrap().links, nested.links);
    let reclaimed = fs::read(&path).unwrap();
    assert_eq!(
        u32::from_le_bytes(reclaimed[1036..1040].try_into().unwrap()),
        free_while_open + retained_blocks
    );
    assert_eq!(
        u32::from_le_bytes(reclaimed[1036..1040].try_into().unwrap()),
        free_before_removal + released
    );
    assert_eq!(
        u32::from_le_bytes(reclaimed[1040..1044].try_into().unwrap()),
        original_free_inodes + 2
    );
    assert_eq!(
        u16::from_le_bytes(reclaimed[2064..2066].try_into().unwrap()),
        original_directories
    );
    for number in removed {
        let offset = inode_offset(&reclaimed, number);
        assert!(
            reclaimed[offset..offset + 128]
                .iter()
                .all(|byte| *byte == 0)
        );
    }
    drop(volume);
    let mut service = Ext2::mount(FileDevice::new(File::open(&path).unwrap())).unwrap();
    let request = Request::new(
        SessionId(7),
        ProcessId(17),
        Handle(0),
        Operation::FileInfo(b"/nested"),
    );
    let Some(Reply::FileInfo(info)) = service
        .execute(request, |pid| {
            assert_eq!(pid, ProcessId(17));
            Ok(Credentials::new(UserId(0), GroupId(0)))
        })
        .unwrap()
    else {
        panic!("Expected directory metadata");
    };
    assert_eq!(info.identity, u64::from(nested.number.0));
    assert_eq!(info.mode, u32::from(nested.mode));
}

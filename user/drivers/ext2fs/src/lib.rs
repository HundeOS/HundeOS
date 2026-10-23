#![no_std]

extern crate alloc;

mod open;
mod service;
mod write;

pub use open::Access;
pub use service::Ext2;

#[cfg(feature = "component")]
hunde::filesystem_component!(
    Ext2<hunde::block_host::HostedDevice>,
    b"Rust EXT2 component panicked"
);

use alloc::vec::Vec;
use hunde::block::{self, Device, Sector};

const BLOCK_SIZE: usize = 1024;
const POINTERS_PER_BLOCK: u32 = BLOCK_SIZE as u32 / 4;
const DIRECT_BLOCKS: u32 = 12;
const MAX_FILE_BLOCKS: u32 =
    DIRECT_BLOCKS + POINTERS_PER_BLOCK + POINTERS_PER_BLOCK * POINTERS_PER_BLOCK;
const DIRECTORY: u16 = 0x4000;
const REGULAR: u16 = 0x8000;
const TYPE_MASK: u16 = 0xf000;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Device(block::Error),
    Corrupt,
    Unsupported,
    InvalidPath,
    NotFound,
    NotDirectory,
    NotFile,
    NoSpace,
    FileTooLarge,
    RecoveryRequired,
    AlreadyExists,
    TooManyLinks,
    IsDirectory,
    NotEmpty,
    UnknownHandle,
    AccessDenied,
    HandleLimit,
    Inheritance(hunde::abi::IoError),
}

impl From<block::Error> for Error {
    fn from(error: block::Error) -> Self {
        Self::Device(error)
    }
}

impl From<hunde::abi::IoError> for Error {
    fn from(error: hunde::abi::IoError) -> Self {
        Self::Inheritance(error)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct InodeNumber(pub u32);

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Inode {
    pub number: InodeNumber,
    pub mode: u16,
    pub user: u16,
    pub group: u16,
    pub size: u64,
    pub links: u16,
    sectors: u32,
    flags: u32,
    blocks: [u32; 15],
}

impl Inode {
    fn new(number: InodeNumber, mode: u16, user: u16, group: u16) -> Self {
        Self {
            number,
            mode,
            user,
            group,
            size: 0,
            links: if mode & TYPE_MASK == DIRECTORY { 2 } else { 1 },
            sectors: 0,
            flags: 0,
            blocks: [0; 15],
        }
    }

    pub fn is_directory(&self) -> bool {
        self.mode & TYPE_MASK == DIRECTORY
    }
    pub fn is_file(&self) -> bool {
        self.mode & TYPE_MASK == REGULAR
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    pub inode: InodeNumber,
    pub name: Vec<u8>,
}

#[derive(Clone, Copy)]
struct Group {
    inode_table: u32,
    block_bitmap: u32,
    inode_bitmap: u32,
}

struct DirectoryRead {
    entry: Entry,
    next_offset: u64,
}

pub struct Volume<D: Device> {
    device: D,
    blocks: u32,
    blocks_per_group: u32,
    inodes: u32,
    inodes_per_group: u32,
    inode_size: u16,
    directory_types: bool,
    groups: Vec<Group>,
    sparse_super: bool,
    reserved_gdt_blocks: u16,
    recovery_required: bool,
    open_files: Vec<open::Description>,
    next_handle: i64,
    grants: hunde::inheritance::Grants,
}

fn read_u16(bytes: &[u8], offset: usize) -> Result<u16, Error> {
    let data = bytes
        .get(offset..offset.checked_add(2).ok_or(Error::Corrupt)?)
        .ok_or(Error::Corrupt)?;
    Ok(u16::from_le_bytes(
        data.try_into().map_err(|_| Error::Corrupt)?,
    ))
}

fn read_u32(bytes: &[u8], offset: usize) -> Result<u32, Error> {
    let data = bytes
        .get(offset..offset.checked_add(4).ok_or(Error::Corrupt)?)
        .ok_or(Error::Corrupt)?;
    Ok(u32::from_le_bytes(
        data.try_into().map_err(|_| Error::Corrupt)?,
    ))
}

fn read_block(device: &mut impl Device, number: u32) -> Result<[u8; BLOCK_SIZE], Error> {
    let mut data = [0; BLOCK_SIZE];
    device.read(Sector(u64::from(number) * 2), &mut data)?;
    Ok(data)
}

impl<D: Device> Volume<D> {
    pub fn mount(mut device: D) -> Result<Self, Error> {
        if device.sector_count() < 4 {
            return Err(Error::Corrupt);
        }
        let superblock = read_block(&mut device, 1)?;
        if read_u16(&superblock, 56)? != 0xef53 {
            return Err(Error::Corrupt);
        }
        let revision = read_u32(&superblock, 76)?;
        if read_u32(&superblock, 24)? != 0 || revision > 1 {
            return Err(Error::Unsupported);
        }
        let incompatible = if revision == 1 {
            read_u32(&superblock, 96)?
        } else {
            0
        };
        let readonly = if revision == 1 {
            read_u32(&superblock, 100)?
        } else {
            0
        };
        if revision == 1 && read_u32(&superblock, 92)? & 4 != 0 {
            return Err(Error::Unsupported);
        }
        if incompatible & !2 != 0 || readonly & !3 != 0 {
            return Err(Error::Unsupported);
        }
        let blocks = read_u32(&superblock, 4)?;
        let inodes = read_u32(&superblock, 0)?;
        let blocks_per_group = read_u32(&superblock, 32)?;
        let inodes_per_group = read_u32(&superblock, 40)?;
        let inode_size = if revision == 0 {
            128
        } else {
            read_u16(&superblock, 88)?
        };
        if blocks < 3
            || read_u32(&superblock, 20)? != 1
            || u64::from(blocks) * 2 > device.sector_count()
            || blocks_per_group == 0
            || blocks_per_group > 8192
            || inodes_per_group == 0
            || inodes_per_group > 8192
            || inodes < 2
            || inode_size < 128
            || inode_size as usize > BLOCK_SIZE
            || !inode_size.is_power_of_two()
        {
            return Err(Error::Corrupt);
        }
        let group_count = (blocks - 1).div_ceil(blocks_per_group);
        if group_count > 32 {
            return Err(Error::Unsupported);
        }
        if u64::from(inodes) > u64::from(group_count) * u64::from(inodes_per_group) {
            return Err(Error::Corrupt);
        }
        let descriptors = read_block(&mut device, 2)?;
        let inode_blocks =
            (u64::from(inodes_per_group) * u64::from(inode_size)).div_ceil(BLOCK_SIZE as u64);
        let mut groups = Vec::new();
        for index in 0..group_count as usize {
            let inode_table = read_u32(&descriptors, index * 32 + 8)?;
            if inode_table < 3 || u64::from(inode_table) + inode_blocks > u64::from(blocks) {
                return Err(Error::Corrupt);
            }
            groups.push(Group {
                inode_table,
                block_bitmap: read_u32(&descriptors, index * 32)?,
                inode_bitmap: read_u32(&descriptors, index * 32 + 4)?,
            });
        }
        let mut volume = Self {
            device,
            blocks,
            blocks_per_group,
            inodes,
            inodes_per_group,
            inode_size,
            directory_types: incompatible & 2 != 0,
            groups,
            sparse_super: readonly & 1 != 0,
            reserved_gdt_blocks: if revision == 1 && read_u32(&superblock, 92)? & 0x10 != 0 {
                read_u16(&superblock, 206)?
            } else {
                0
            },
            recovery_required: false,
            open_files: Vec::new(),
            next_handle: 1 << 32,
            grants: hunde::inheritance::Grants::default(),
        };
        if !volume.inode(InodeNumber(2))?.is_directory() {
            return Err(Error::Corrupt);
        }
        Ok(volume)
    }

    fn block(&mut self, number: u32) -> Result<[u8; BLOCK_SIZE], Error> {
        if self.recovery_required {
            return Err(Error::RecoveryRequired);
        }
        if number == 0 || number >= self.blocks {
            return Err(Error::Corrupt);
        }
        read_block(&mut self.device, number)
    }

    pub fn inode(&mut self, number: InodeNumber) -> Result<Inode, Error> {
        if number.0 == 0 || number.0 > self.inodes {
            return Err(Error::Corrupt);
        }
        let index = number.0 - 1;
        let group = self
            .groups
            .get((index / self.inodes_per_group) as usize)
            .ok_or(Error::Corrupt)?;
        let offset = u64::from(index % self.inodes_per_group) * u64::from(self.inode_size);
        let block = group.inode_table + (offset / BLOCK_SIZE as u64) as u32;
        let data = self.block(block)?;
        let offset = offset as usize % BLOCK_SIZE;
        let mode = read_u16(&data, offset)?;
        let size = u64::from(read_u32(&data, offset + 4)?);
        if mode & TYPE_MASK == REGULAR && read_u32(&data, offset + 108)? != 0 {
            return Err(Error::Unsupported);
        }
        if size > u64::from(MAX_FILE_BLOCKS) * BLOCK_SIZE as u64 {
            return Err(Error::Unsupported);
        }
        if read_u32(&data, offset + 32)? & 0x0008_0000 != 0 {
            return Err(Error::Unsupported);
        }
        let mut blocks = [0; 15];
        for (index, block) in blocks.iter_mut().enumerate() {
            *block = read_u32(&data, offset + 40 + index * 4)?;
        }
        Ok(Inode {
            number,
            mode,
            user: read_u16(&data, offset + 2)?,
            group: read_u16(&data, offset + 24)?,
            size,
            links: read_u16(&data, offset + 26)?,
            sectors: read_u32(&data, offset + 28)?,
            flags: read_u32(&data, offset + 32)?,
            blocks,
        })
    }

    fn pointer(&mut self, block: u32, index: u32) -> Result<u32, Error> {
        if block == 0 {
            return Ok(0);
        }
        read_u32(&self.block(block)?, index as usize * 4)
    }

    fn data_block(&mut self, inode: &Inode, index: u32) -> Result<u32, Error> {
        if index < DIRECT_BLOCKS {
            return Ok(inode.blocks[index as usize]);
        }
        let index = index - DIRECT_BLOCKS;
        if index < POINTERS_PER_BLOCK {
            return self.pointer(inode.blocks[12], index);
        }
        let index = index - POINTERS_PER_BLOCK;
        if index >= POINTERS_PER_BLOCK * POINTERS_PER_BLOCK {
            return Err(Error::Unsupported);
        }
        let table = self.pointer(inode.blocks[13], index / POINTERS_PER_BLOCK)?;
        self.pointer(table, index % POINTERS_PER_BLOCK)
    }

    fn read_inode(
        &mut self,
        inode: &Inode,
        offset: u64,
        destination: &mut [u8],
    ) -> Result<usize, Error> {
        let count = destination
            .len()
            .min(inode.size.saturating_sub(offset) as usize);
        let mut copied = 0;
        while copied < count {
            let position = offset + copied as u64;
            let block = self.data_block(inode, (position / BLOCK_SIZE as u64) as u32)?;
            let within = position as usize % BLOCK_SIZE;
            let take = (BLOCK_SIZE - within).min(count - copied);
            let target = &mut destination[copied..copied + take];
            if block == 0 {
                target.fill(0);
            } else {
                target.copy_from_slice(&self.block(block)?[within..within + take]);
            }
            copied += take;
        }
        Ok(copied)
    }

    pub fn read(
        &mut self,
        number: InodeNumber,
        offset: u64,
        destination: &mut [u8],
    ) -> Result<usize, Error> {
        let inode = self.inode(number)?;
        if !inode.is_file() {
            return Err(Error::NotFile);
        }
        self.read_inode(&inode, offset, destination)
    }

    fn next_entry(
        &mut self,
        inode: &Inode,
        mut offset: u64,
    ) -> Result<Option<DirectoryRead>, Error> {
        while offset < inode.size {
            let mut header = [0; 8];
            if self.read_inode(inode, offset, &mut header)? != header.len() {
                return Err(Error::Corrupt);
            }
            let number = read_u32(&header, 0)?;
            let length = u64::from(read_u16(&header, 4)?);
            let name_length = if self.directory_types {
                header[6] as usize
            } else {
                read_u16(&header, 6)? as usize
            };
            if length < 8
                || length % 4 != 0
                || length > BLOCK_SIZE as u64 - offset % BLOCK_SIZE as u64
                || length > inode.size - offset
                || name_length > length as usize - 8
                || name_length > 255
            {
                return Err(Error::Corrupt);
            }
            let start = offset + 8;
            offset += length;
            if number == 0 {
                continue;
            }
            if number > self.inodes || name_length == 0 {
                return Err(Error::Corrupt);
            }
            let mut name = alloc::vec![0; name_length];
            if self.read_inode(inode, start, &mut name)? != name_length
                || name.contains(&0)
                || name.contains(&b'/')
            {
                return Err(Error::Corrupt);
            }
            return Ok(Some(DirectoryRead {
                entry: Entry {
                    inode: InodeNumber(number),
                    name,
                },
                next_offset: offset,
            }));
        }
        Ok(None)
    }

    pub fn entries(&mut self, number: InodeNumber) -> Result<Vec<Entry>, Error> {
        let inode = self.inode(number)?;
        if !inode.is_directory() {
            return Err(Error::NotDirectory);
        }
        let mut entries = Vec::new();
        let mut offset = 0;
        while let Some(result) = self.next_entry(&inode, offset)? {
            entries.push(result.entry);
            offset = result.next_offset;
        }
        Ok(entries)
    }

    pub fn lookup(&mut self, path: &[u8]) -> Result<Inode, Error> {
        if !path.starts_with(b"/") || path.len() >= 1024 || path.contains(&0) {
            return Err(Error::InvalidPath);
        }
        let mut current = self.inode(InodeNumber(2))?;
        for (depth, name) in path
            .split(|byte| *byte == b'/')
            .filter(|part| !part.is_empty())
            .enumerate()
        {
            if depth >= 32 {
                return Err(Error::InvalidPath);
            }
            if !current.is_directory() {
                return Err(Error::NotDirectory);
            }
            let mut offset = 0;
            let mut found = None;
            while let Some(result) = self.next_entry(&current, offset)? {
                if result.entry.name == name {
                    found = Some(result.entry.inode);
                    break;
                }
                offset = result.next_offset;
            }
            current = self.inode(found.ok_or(Error::NotFound)?)?;
        }
        if path.ends_with(b"/") && !current.is_directory() {
            return Err(Error::NotDirectory);
        }
        Ok(current)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    pub(super) struct Disk {
        pub(super) bytes: Vec<u8>,
        fail: bool,
        pub(super) writes: usize,
        pub(super) fail_writes: Vec<usize>,
        pub(super) reads: usize,
        pub(super) fail_reads: Vec<usize>,
    }

    impl Disk {
        pub(super) fn new() -> Self {
            let mut disk = Self {
                bytes: alloc::vec![0; 64 * BLOCK_SIZE],
                fail: false,
                writes: 0,
                fail_writes: Vec::new(),
                reads: 0,
                fail_reads: Vec::new(),
            };
            disk.word(1024, 32);
            disk.word(1028, 64);
            disk.word(1044, 1);
            disk.word(1056, 8192);
            disk.word(1064, 32);
            disk.half(1080, 0xef53);
            disk.word(1100, 1);
            disk.half(1112, 128);
            disk.word(1120, 2);
            disk.word(2048, 3);
            disk.word(2052, 4);
            disk.word(2056, 5);
            disk.word(1036, 53);
            disk.half(2060, 53);
            disk.word(1108, 11);
            disk.word(1040, 20);
            disk.half(2062, 20);
            disk.half(2064, 1);
            disk.bytes[4 * BLOCK_SIZE..4 * BLOCK_SIZE + 2].copy_from_slice(&[255, 15]);
            disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
            for number in 11..64 {
                let bit = number - 1;
                disk.bytes[3 * BLOCK_SIZE + bit / 8] &= !(1 << (bit % 8));
            }
            let root = Self::inode_offset(2);
            disk.half(root, DIRECTORY | 0o755);
            disk.word(root + 4, 1024);
            disk.half(root + 26, 2);
            disk.word(root + 28, 2);
            disk.word(root + 40, 9);
            let file = Self::inode_offset(12);
            disk.half(file, REGULAR | 0o644);
            disk.half(file + 2, 7);
            disk.half(file + 24, 8);
            disk.word(file + 4, 5);
            disk.half(file + 26, 1);
            disk.word(file + 28, 2);
            disk.word(file + 40, 10);
            disk.bytes[10 * BLOCK_SIZE..10 * BLOCK_SIZE + 5].copy_from_slice(b"hello");
            disk.entry(9 * BLOCK_SIZE, 2, 12, b".");
            disk.entry(9 * BLOCK_SIZE + 12, 2, 12, b"..");
            disk.entry(9 * BLOCK_SIZE + 24, 12, 1000, b"file");
            disk
        }

        pub(super) fn inode_offset(number: usize) -> usize {
            5 * BLOCK_SIZE + (number - 1) * 128
        }
        pub(super) fn half(&mut self, offset: usize, value: u16) {
            self.bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
        }
        pub(super) fn word(&mut self, offset: usize, value: u32) {
            self.bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
        }
        pub(super) fn entry(&mut self, offset: usize, inode: u32, length: u16, name: &[u8]) {
            self.word(offset, inode);
            self.half(offset + 4, length);
            self.bytes[offset + 6] = name.len() as u8;
            self.bytes[offset + 7] = if inode == 2 { 2 } else { 1 };
            self.bytes[offset + 8..offset + 8 + name.len()].copy_from_slice(name);
        }
    }

    impl Device for Disk {
        fn sector_count(&self) -> u64 {
            self.bytes.len() as u64 / 512
        }
        fn read(&mut self, sector: Sector, destination: &mut [u8]) -> Result<(), block::Error> {
            self.reads += 1;
            if self.fail || self.fail_reads.contains(&self.reads) {
                return Err(block::Error::Native(hunde::abi::IoError(-71)));
            }
            let start = usize::try_from(sector.0).map_err(|_| block::Error::InvalidTransfer)? * 512;
            let source = self
                .bytes
                .get(start..start + destination.len())
                .ok_or(block::Error::InvalidTransfer)?;
            destination.copy_from_slice(source);
            Ok(())
        }
        fn write(&mut self, sector: Sector, source: &[u8]) -> Result<(), block::Error> {
            self.writes += 1;
            let start = sector.0 as usize * block::SECTOR_SIZE;
            if self.fail_writes.contains(&self.writes) {
                self.bytes[start..start + source.len() / 2]
                    .copy_from_slice(&source[..source.len() / 2]);
                return Err(block::Error::Native(hunde::abi::IoError(-72)));
            }
            self.bytes[start..start + source.len()].copy_from_slice(source);
            Ok(())
        }
    }

    #[test]
    fn mounts_resolves_lists_and_reads_metadata_and_partial_file_data() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let inode = volume.lookup(b"//./file").unwrap();
        assert_eq!(
            (
                inode.number,
                inode.user,
                inode.group,
                inode.size,
                inode.links
            ),
            (InodeNumber(12), 7, 8, 5, 1)
        );
        assert!(inode.is_file());
        assert!(volume.lookup(b"/").unwrap().is_directory());
        let entries = volume.entries(InodeNumber(2)).unwrap();
        assert_eq!(
            entries
                .iter()
                .map(|entry| entry.name.as_slice())
                .collect::<Vec<_>>(),
            [b".".as_slice(), b"..", b"file"]
        );
        let mut data = [99; 8];
        assert_eq!(volume.read(inode.number, 2, &mut data), Ok(3));
        assert_eq!(&data[..3], b"llo");
        assert_eq!(&data[3..], &[99; 5]);
        assert_eq!(volume.read(inode.number, 5, &mut data), Ok(0));
        assert_eq!(volume.read(inode.number, u64::MAX, &mut data), Ok(0));
        assert_eq!(volume.read(inode.number, 0, &mut []), Ok(0));
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn validates_superblock_geometry_features_revision_and_device_bounds() {
        for (offset, value, expected) in [
            (1056, 0, Error::Corrupt),
            (1064, 0, Error::Corrupt),
            (1056, 8193, Error::Corrupt),
            (1064, 8193, Error::Corrupt),
            (1028, 65, Error::Corrupt),
            (1044, 0, Error::Corrupt),
            (1048, 1, Error::Unsupported),
            (1100, 2, Error::Unsupported),
            (1120, 0x40, Error::Unsupported),
            (1116, 4, Error::Unsupported),
            (1124, 0x400, Error::Unsupported),
            (2056, 63, Error::Corrupt),
            (1024, 33, Error::Corrupt),
        ] {
            let mut disk = Disk::new();
            disk.word(offset, value);
            assert!(
                matches!(Volume::mount(disk), Err(error) if error == expected),
                "offset {offset}"
            );
        }
        for size in [0, 64, 129, 2048] {
            let mut disk = Disk::new();
            disk.half(1112, size);
            assert!(matches!(Volume::mount(disk), Err(Error::Corrupt)));
        }
        let mut disk = Disk::new();
        disk.half(1080, 0);
        assert!(matches!(Volume::mount(disk), Err(Error::Corrupt)));
    }

    #[test]
    fn reads_inodes_from_a_second_block_group() {
        let mut disk = Disk::new();
        disk.bytes.resize(9000 * BLOCK_SIZE, 0);
        disk.word(1028, 9000);
        disk.word(1024, 64);
        disk.word(2048 + 32 + 8, 8195);
        let inode = 8195 * BLOCK_SIZE + 128;
        disk.half(inode, REGULAR | 0o600);
        disk.word(inode + 4, 4);
        disk.word(inode + 40, 8999);
        disk.bytes[8999 * BLOCK_SIZE..8999 * BLOCK_SIZE + 4].copy_from_slice(b"last");
        disk.word(9 * BLOCK_SIZE + 24, 34);
        let mut volume = Volume::mount(disk).unwrap();
        let file = volume.lookup(b"/file").unwrap();
        assert_eq!(file.number, InodeNumber(34));
        let mut data = [0; 4];
        assert_eq!(volume.read(file.number, 0, &mut data), Ok(4));
        assert_eq!(&data, b"last");
    }

    #[test]
    fn supports_revision_zero_and_directory_records_without_type_bytes() {
        let mut disk = Disk::new();
        disk.word(1100, 0);
        disk.bytes[9 * BLOCK_SIZE + 7] = 0;
        disk.bytes[9 * BLOCK_SIZE + 19] = 0;
        disk.bytes[9 * BLOCK_SIZE + 31] = 0;
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.lookup(b"/file").unwrap().size, 5);
    }

    #[test]
    fn validates_paths_inode_numbers_and_node_types() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        assert_eq!(volume.inode(InodeNumber(0)), Err(Error::Corrupt));
        assert_eq!(volume.inode(InodeNumber(33)), Err(Error::Corrupt));
        for path in [b"relative".as_slice(), b"/fi\0le", &[b'/'; 1024]] {
            assert_eq!(volume.lookup(path), Err(Error::InvalidPath));
        }
        assert_eq!(volume.lookup(b"/missing"), Err(Error::NotFound));
        assert_eq!(volume.lookup(b"/file/"), Err(Error::NotDirectory));
        assert_eq!(volume.lookup(b"/file/child"), Err(Error::NotDirectory));
        assert_eq!(volume.entries(InodeNumber(12)), Err(Error::NotDirectory));
        assert_eq!(
            volume.read(InodeNumber(2), 0, &mut [0]),
            Err(Error::NotFile)
        );
    }

    #[test]
    fn reads_sparse_direct_single_and_double_indirect_blocks() {
        let mut disk = Disk::new();
        let file = Disk::inode_offset(12);
        disk.word(
            file + 4,
            (DIRECT_BLOCKS + POINTERS_PER_BLOCK + 1) * BLOCK_SIZE as u32,
        );
        disk.word(file + 40 + 12 * 4, 11);
        disk.word(11 * BLOCK_SIZE, 14);
        disk.word(file + 40 + 13 * 4, 12);
        disk.word(12 * BLOCK_SIZE, 13);
        disk.word(13 * BLOCK_SIZE, 15);
        disk.bytes[14 * BLOCK_SIZE] = 71;
        disk.bytes[15 * BLOCK_SIZE] = 83;
        let mut volume = Volume::mount(disk).unwrap();
        for (offset, expected) in [
            (0, b'h'),
            (1024, 0),
            (12 * 1024, 71),
            ((12 + 256) * 1024, 83),
        ] {
            let mut data = [255];
            assert_eq!(volume.read(InodeNumber(12), offset, &mut data), Ok(1));
            assert_eq!(data, [expected]);
        }
        let mut data = [255; 4];
        assert_eq!(volume.read(InodeNumber(12), 1022, &mut data), Ok(4));
        assert_eq!(data, [0; 4]);
    }

    #[test]
    fn rejects_malformed_directory_records_without_looping_or_panicking() {
        for length in [0, 4, 9, 1028] {
            let mut disk = Disk::new();
            disk.half(9 * BLOCK_SIZE + 4, length);
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(volume.entries(InodeNumber(2)), Err(Error::Corrupt));
        }
        let mut disk = Disk::new();
        disk.bytes[9 * BLOCK_SIZE + 6] = 5;
        assert_eq!(
            Volume::mount(disk).unwrap().entries(InodeNumber(2)),
            Err(Error::Corrupt)
        );
        let mut disk = Disk::new();
        disk.word(9 * BLOCK_SIZE, 33);
        assert_eq!(
            Volume::mount(disk).unwrap().entries(InodeNumber(2)),
            Err(Error::Corrupt)
        );
    }

    #[test]
    fn propagates_device_errors_and_rejects_out_of_range_block_pointers() {
        let mut disk = Disk::new();
        disk.fail = true;
        assert!(matches!(
            Volume::mount(disk),
            Err(Error::Device(block::Error::Native(hunde::abi::IoError(
                -71
            ))))
        ));
        let mut disk = Disk::new();
        disk.word(Disk::inode_offset(12) + 40, 64);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.read(InodeNumber(12), 0, &mut [0]),
            Err(Error::Corrupt)
        );
        volume.device.fail = true;
        assert_eq!(
            volume.inode(InodeNumber(12)),
            Err(Error::Device(block::Error::Native(hunde::abi::IoError(
                -71
            ))))
        );
    }
}

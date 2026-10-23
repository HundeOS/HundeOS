#![no_std]

extern crate alloc;

mod service;
mod write;

pub use service::Fat32;

#[cfg(feature = "component")]
hunde::filesystem_component!(
    Fat32<hunde::block_host::HostedDevice>,
    b"Rust FAT32 component panicked"
);

use alloc::string::String;
use alloc::vec::Vec;
use hunde::block::{self, Device, SECTOR_SIZE, Sector};

const CLUSTER_MASK: u32 = 0x0fff_ffff;
const RESERVED_CLUSTER: u32 = 0x0fff_fff0;
const END_OF_CHAIN: u32 = 0x0fff_fff8;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Device(block::Error),
    Corrupt,
    Unsupported,
    InvalidPath,
    NotFound,
    NotDirectory,
    NotFile,
    OutOfMemory,
    NoSpace,
    FileTooLarge,
    AlreadyExists,
    NotEmpty,
    AccessDenied,
    RecoveryRequired,
}

impl From<block::Error> for Error {
    fn from(error: block::Error) -> Self {
        Self::Device(error)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct Cluster(u32);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Kind {
    File,
    Directory,
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Entry {
    pub name: String,
    alias: Option<String>,
    pub kind: Kind,
    pub size: u32,
    cluster: Cluster,
    attributes: u8,
    location: Option<DirectorySlot>,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
struct DirectorySlot {
    sector: Sector,
    offset: usize,
}

impl DirectorySlot {
    fn new(sector: Sector, offset: usize) -> Self {
        Self { sector, offset }
    }
}

impl Entry {
    fn new(
        name: String,
        kind: Kind,
        size: u32,
        cluster: Cluster,
        attributes: u8,
        location: Option<DirectorySlot>,
    ) -> Self {
        Self {
            name,
            alias: None,
            kind,
            size,
            cluster,
            attributes,
            location,
        }
    }
}

struct Geometry {
    fat_start: u64,
    data_start: u64,
    sectors_per_cluster: u32,
    clusters: u32,
    root: Cluster,
    fat_base: u64,
    fat_size: u64,
    fats: u64,
    mirrored: bool,
    fsinfo: Option<Sector>,
    backup_fsinfo: Option<Sector>,
}

fn word(bytes: &[u8], offset: usize) -> u16 {
    u16::from_le_bytes([bytes[offset], bytes[offset + 1]])
}

fn double_word(bytes: &[u8], offset: usize) -> u32 {
    u32::from_le_bytes([
        bytes[offset],
        bytes[offset + 1],
        bytes[offset + 2],
        bytes[offset + 3],
    ])
}

impl Geometry {
    fn parse(boot: &[u8; SECTOR_SIZE], sectors: u64) -> Result<Self, Error> {
        if boot[510..512] != [0x55, 0xaa] {
            return Err(Error::Corrupt);
        }
        if word(boot, 11) != SECTOR_SIZE as u16
            || word(boot, 17) != 0
            || word(boot, 19) != 0
            || word(boot, 22) != 0
            || word(boot, 42) != 0
        {
            return Err(Error::Unsupported);
        }
        let sectors_per_cluster = u32::from(boot[13]);
        let reserved = u64::from(word(boot, 14));
        let fats = u64::from(boot[16]);
        let fat_size = u64::from(double_word(boot, 36));
        let total = u64::from(double_word(boot, 32));
        if !sectors_per_cluster.is_power_of_two()
            || sectors_per_cluster > 128
            || reserved == 0
            || fats == 0
            || fat_size == 0
            || total > sectors
        {
            return Err(Error::Corrupt);
        }
        if fats > 2 {
            return Err(Error::Unsupported);
        }
        let data_start = reserved + fats * fat_size;
        let data_sectors = total.checked_sub(data_start).ok_or(Error::Corrupt)?;
        let clusters = data_sectors / u64::from(sectors_per_cluster);
        if clusters < 65525 {
            return Err(Error::Unsupported);
        }
        if clusters + 1 >= u64::from(RESERVED_CLUSTER)
            || fat_size * SECTOR_SIZE as u64 / 4 < clusters + 2
        {
            return Err(Error::Corrupt);
        }
        let flags = word(boot, 40);
        let active_fat = if flags & 0x80 == 0 {
            0
        } else {
            u64::from(flags & 0xf)
        };
        if active_fat >= fats {
            return Err(Error::Corrupt);
        }
        let fsinfo = u64::from(word(boot, 48));
        let backup = u64::from(word(boot, 50));
        let fsinfo = match fsinfo {
            0 | 0xffff => None,
            sector if sector < reserved => Some(Sector(sector)),
            _ => return Err(Error::Corrupt),
        };
        let backup_fsinfo = match (fsinfo, backup) {
            (Some(info), backup) if backup != 0 && backup != 0xffff => {
                let sector = backup + info.0;
                if sector >= reserved {
                    return Err(Error::Corrupt);
                }
                Some(Sector(sector))
            }
            _ => None,
        };
        let geometry = Self {
            fat_start: reserved + active_fat * fat_size,
            data_start,
            sectors_per_cluster,
            clusters: clusters as u32,
            root: Cluster(double_word(boot, 44)),
            fat_base: reserved,
            fat_size,
            fats,
            mirrored: flags & 0x80 == 0,
            fsinfo,
            backup_fsinfo,
        };
        geometry.validate(geometry.root)?;
        Ok(geometry)
    }

    fn validate(&self, cluster: Cluster) -> Result<(), Error> {
        if !(2..=self.clusters + 1).contains(&cluster.0) {
            return Err(Error::Corrupt);
        }
        Ok(())
    }

    fn sector(&self, cluster: Cluster) -> Result<u64, Error> {
        self.validate(cluster)?;
        Ok(self.data_start + u64::from(cluster.0 - 2) * u64::from(self.sectors_per_cluster))
    }
}

struct Chain {
    current: Option<Cluster>,
    slow: Option<Cluster>,
    fast: Option<Cluster>,
    remaining: u32,
}

impl Chain {
    fn new(start: Cluster, clusters: u32) -> Self {
        Self {
            current: Some(start),
            slow: Some(start),
            fast: Some(start),
            remaining: clusters,
        }
    }

    fn advance<D: Device>(
        volume: &mut Volume<D>,
        cluster: Option<Cluster>,
    ) -> Result<Option<Cluster>, Error> {
        match cluster {
            Some(cluster) => volume.next_cluster(cluster),
            None => Ok(None),
        }
    }

    fn next<D: Device>(&mut self, volume: &mut Volume<D>) -> Result<Option<Cluster>, Error> {
        let Some(current) = self.current else {
            return Ok(None);
        };
        self.remaining = self.remaining.checked_sub(1).ok_or(Error::Corrupt)?;
        self.slow = Self::advance(volume, self.slow)?;
        self.fast = Self::advance(volume, self.fast)?;
        self.fast = Self::advance(volume, self.fast)?;
        if self.slow.is_some() && self.slow == self.fast {
            return Err(Error::Corrupt);
        }
        self.current = self.slow;
        Ok(Some(current))
    }
}

enum Slot {
    End,
    Skip,
    Entry(Entry),
}

struct LongName {
    units: [u16; 260],
    remaining: Option<u8>,
    checksum: u8,
}

impl LongName {
    fn new() -> Self {
        Self {
            units: [0xffff; 260],
            remaining: None,
            checksum: 0,
        }
    }

    fn push(&mut self, record: &[u8; 32]) {
        let ordinal = record[0] & 0x1f;
        if record[0] & 0x40 != 0 {
            self.units.fill(0xffff);
            self.remaining = Some(ordinal);
            self.checksum = record[13];
        }
        if ordinal == 0
            || ordinal > 20
            || record[0] & 0xa0 != 0
            || self.remaining != Some(ordinal)
            || record[13] != self.checksum
            || record[12] != 0
            || word(record, 26) != 0
        {
            self.remaining = None;
            return;
        }
        for (index, offset) in [1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30]
            .iter()
            .enumerate()
        {
            self.units[(usize::from(ordinal) - 1) * 13 + index] = word(record, *offset);
        }
        self.remaining = Some(ordinal - 1);
    }

    fn finish(&mut self, record: &[u8; 32]) -> Result<Option<String>, Error> {
        let valid = self.remaining.take() == Some(0)
            && record[..11]
                .iter()
                .fold(0u8, |sum, byte| sum.rotate_right(1).wrapping_add(*byte))
                == self.checksum;
        if !valid {
            return Ok(None);
        }
        let length = self
            .units
            .iter()
            .position(|unit| *unit == 0 || *unit == 0xffff)
            .unwrap_or(self.units.len());
        if length == 0 || length > 255 {
            return Ok(None);
        }
        if self.units[length + 1..].iter().any(|unit| *unit != 0xffff) {
            return Ok(None);
        }
        let mut name = String::new();
        name.try_reserve(length * 3)
            .map_err(|_| Error::OutOfMemory)?;
        for character in char::decode_utf16(self.units[..length].iter().copied()) {
            let Ok(character) = character else {
                return Ok(None);
            };
            if character.is_control() || "\\/:*?\"<>|".contains(character) {
                return Ok(None);
            }
            name.push(character);
        }
        if name == "." || name == ".." || name.len() >= 256 || name.ends_with([' ', '.']) {
            return Ok(None);
        }
        Ok(Some(name))
    }
}

fn short_field(bytes: &[u8]) -> Result<&str, Error> {
    let length = bytes
        .iter()
        .rposition(|byte| *byte != b' ')
        .map_or(0, |index| index + 1);
    let bytes = &bytes[..length];
    if !bytes.is_ascii() {
        return Err(Error::Unsupported);
    }
    if bytes
        .iter()
        .any(|byte| byte.is_ascii_control() || b" \"*+,/:;<=>?[\\]|".contains(byte))
    {
        return Err(Error::Corrupt);
    }
    core::str::from_utf8(bytes).map_err(|_| Error::Corrupt)
}

impl Slot {
    fn parse(bytes: &[u8; 32], geometry: &Geometry) -> Result<Self, Error> {
        if bytes[0] == 0 {
            return Ok(Self::End);
        }
        if bytes[0] == 0xe5 || bytes[11] == 0xf || bytes[11] & 0x8 != 0 {
            return Ok(Self::Skip);
        }
        if bytes[11] & 0xc0 != 0 {
            return Err(Error::Corrupt);
        }
        let base = short_field(&bytes[..8])?;
        let extension = short_field(&bytes[8..11])?;
        if base == "." || base == ".." {
            return Ok(Self::Skip);
        }
        if base.is_empty() || base.contains('.') || extension.contains('.') {
            return Err(Error::Corrupt);
        }
        let mut name = String::new();
        name.try_reserve(12).map_err(|_| Error::OutOfMemory)?;
        name.push_str(base);
        if !extension.is_empty() {
            name.push('.');
            name.push_str(extension);
        }
        let kind = if bytes[11] & 0x10 == 0 {
            Kind::File
        } else {
            Kind::Directory
        };
        let cluster = Cluster(
            ((u32::from(word(bytes, 20)) << 16) | u32::from(word(bytes, 26))) & CLUSTER_MASK,
        );
        let size = double_word(bytes, 28);
        if kind == Kind::Directory || size != 0 || cluster.0 != 0 {
            geometry.validate(cluster)?;
        }
        if kind == Kind::Directory && size != 0 {
            return Err(Error::Corrupt);
        }
        Ok(Self::Entry(Entry::new(
            name, kind, size, cluster, bytes[11], None,
        )))
    }
}

pub struct Volume<D> {
    device: D,
    geometry: Geometry,
    recovery_required: bool,
}

impl<D: Device> Volume<D> {
    pub fn mount(mut device: D) -> Result<Self, Error> {
        let mut boot = [0; SECTOR_SIZE];
        device.read(Sector(0), &mut boot)?;
        let geometry = Geometry::parse(&boot, device.sector_count())?;
        Ok(Self {
            device,
            geometry,
            recovery_required: false,
        })
    }

    fn next_cluster(&mut self, cluster: Cluster) -> Result<Option<Cluster>, Error> {
        self.geometry.validate(cluster)?;
        let offset = u64::from(cluster.0) * 4;
        let mut sector = [0; SECTOR_SIZE];
        self.device.read(
            Sector(self.geometry.fat_start + offset / SECTOR_SIZE as u64),
            &mut sector,
        )?;
        let next = double_word(&sector, (offset % SECTOR_SIZE as u64) as usize) & CLUSTER_MASK;
        if next >= END_OF_CHAIN {
            return Ok(None);
        }
        let next = Cluster(next);
        self.geometry.validate(next)?;
        Ok(Some(next))
    }

    fn scan_directory(
        &mut self,
        start: Cluster,
        mut visit: impl FnMut(Entry) -> Result<bool, Error>,
    ) -> Result<(), Error> {
        let mut chain = Chain::new(start, self.geometry.clusters);
        let mut long_name = LongName::new();
        while let Some(cluster) = chain.next(self)? {
            let first = self.geometry.sector(cluster)?;
            for offset in 0..self.geometry.sectors_per_cluster {
                let mut sector = [0; SECTOR_SIZE];
                self.device
                    .read(Sector(first + u64::from(offset)), &mut sector)?;
                for (index, bytes) in sector.as_chunks::<32>().0.iter().enumerate() {
                    if bytes[0] != 0 && bytes[0] != 0xe5 && bytes[11] == 0xf {
                        long_name.push(bytes);
                        continue;
                    }
                    let name = long_name.finish(bytes)?;
                    match Slot::parse(bytes, &self.geometry)? {
                        Slot::End => return Ok(()),
                        Slot::Skip => {}
                        Slot::Entry(mut entry) => {
                            if let Some(name) = name {
                                entry.alias = Some(core::mem::replace(&mut entry.name, name));
                            }
                            entry.location = Some(DirectorySlot::new(
                                Sector(first + u64::from(offset)),
                                index * 32,
                            ));
                            if !visit(entry)? {
                                return Ok(());
                            }
                        }
                    }
                }
            }
        }
        Ok(())
    }

    pub fn entry(&mut self, path: &str) -> Result<Entry, Error> {
        if self.recovery_required {
            return Err(Error::RecoveryRequired);
        }
        validate_path(path)?;
        let mut entry = Entry::new(
            String::new(),
            Kind::Directory,
            0,
            self.geometry.root,
            0x10,
            None,
        );
        if path == "/" {
            return Ok(entry);
        }
        for part in path[1..].split('/') {
            if entry.kind != Kind::Directory {
                return Err(Error::NotDirectory);
            }
            let mut found = None;
            self.scan_directory(entry.cluster, |candidate| {
                if candidate.name.eq_ignore_ascii_case(part)
                    || candidate
                        .alias
                        .as_ref()
                        .is_some_and(|alias| alias.eq_ignore_ascii_case(part))
                {
                    found = Some(candidate);
                    return Ok(false);
                }
                Ok(true)
            })?;
            entry = found.ok_or(Error::NotFound)?;
        }
        Ok(entry)
    }

    pub fn list(&mut self, path: &str, offset: u64, capacity: usize) -> Result<Vec<Entry>, Error> {
        let directory = self.entry(path)?;
        if directory.kind != Kind::Directory {
            return Err(Error::NotDirectory);
        }
        let mut entries = Vec::new();
        if capacity == 0 {
            return Ok(entries);
        }
        let mut skipped = 0;
        self.scan_directory(directory.cluster, |entry| {
            if skipped < offset {
                skipped += 1;
                return Ok(true);
            }
            entries.try_reserve(1).map_err(|_| Error::OutOfMemory)?;
            entries.push(entry);
            Ok(entries.len() < capacity)
        })?;
        Ok(entries)
    }

    pub fn read(
        &mut self,
        path: &str,
        offset: u64,
        destination: &mut [u8],
    ) -> Result<usize, Error> {
        let file = self.entry(path)?;
        if file.kind != Kind::File {
            return Err(Error::NotFile);
        }
        if offset >= u64::from(file.size) || destination.is_empty() {
            return Ok(0);
        }
        let length = destination
            .len()
            .min((u64::from(file.size) - offset) as usize);
        let cluster_bytes = u64::from(self.geometry.sectors_per_cluster) * SECTOR_SIZE as u64;
        let mut chain = Chain::new(file.cluster, self.geometry.clusters);
        for _ in 0..offset / cluster_bytes {
            chain.next(self)?.ok_or(Error::Corrupt)?;
        }
        let mut within = offset % cluster_bytes;
        let mut completed = 0;
        while completed < length {
            let cluster = chain.next(self)?.ok_or(Error::Corrupt)?;
            let first = self.geometry.sector(cluster)?;
            while within < cluster_bytes && completed < length {
                let mut sector = [0; SECTOR_SIZE];
                self.device
                    .read(Sector(first + within / SECTOR_SIZE as u64), &mut sector)?;
                let start = (within % SECTOR_SIZE as u64) as usize;
                let count = (SECTOR_SIZE - start).min(length - completed);
                destination[completed..completed + count]
                    .copy_from_slice(&sector[start..start + count]);
                completed += count;
                within += count as u64;
            }
            within = 0;
        }
        Ok(completed)
    }
}

fn validate_path(path: &str) -> Result<(), Error> {
    if !path.starts_with('/') || path.len() >= 1024 || path.contains('\0') {
        return Err(Error::InvalidPath);
    }
    if path != "/"
        && path[1..].split('/').any(|part| {
            part.is_empty() || part == "." || part == ".." || part.encode_utf16().count() > 255
        })
    {
        return Err(Error::InvalidPath);
    }
    Ok(())
}

#[cfg(test)]
mod tests;

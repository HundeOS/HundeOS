use super::*;
use alloc::collections::{BTreeMap, BTreeSet, btree_map::Entry};
use core::ops::Range;

struct Change {
    original: [u8; BLOCK_SIZE],
    updated: [u8; BLOCK_SIZE],
}

struct Mutation<'a, D: Device> {
    volume: &'a mut Volume<D>,
    changes: BTreeMap<u32, Change>,
}

fn store_u16(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn store_u32(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

pub(super) fn validate_name(name: &[u8]) -> Result<(), Error> {
    if name.is_empty()
        || name.len() > 255
        || name == b"."
        || name == b".."
        || name.contains(&0)
        || name.contains(&b'/')
    {
        return Err(Error::InvalidPath);
    }
    Ok(())
}

struct InodeLocation {
    block: u32,
    offset: usize,
}

impl InodeLocation {
    fn new(block: u32, offset: usize) -> Self {
        Self { block, offset }
    }
}

impl<D: Device> Volume<D> {
    fn first_inode(&self, superblock: &[u8]) -> Result<u32, Error> {
        let first = if read_u32(superblock, 76)? == 0 {
            11
        } else {
            read_u32(superblock, 84)?
        };
        if first < 11 || first > self.inodes.saturating_add(1) {
            return Err(Error::Corrupt);
        }
        Ok(first)
    }

    fn inode_location(&self, number: InodeNumber) -> Result<InodeLocation, Error> {
        if number.0 == 0 || number.0 > self.inodes {
            return Err(Error::Corrupt);
        }
        let index = number.0 - 1;
        let group = &self.groups[(index / self.inodes_per_group) as usize];
        let offset = (index % self.inodes_per_group) * u32::from(self.inode_size);
        Ok(InodeLocation::new(
            group.inode_table + offset / BLOCK_SIZE as u32,
            offset as usize % BLOCK_SIZE,
        ))
    }

    fn group_range(&self, group: usize) -> Range<u32> {
        let start = 1 + group as u32 * self.blocks_per_group;
        start..(start + self.blocks_per_group).min(self.blocks)
    }

    fn has_superblock(&self, group: usize) -> bool {
        if !self.sparse_super || group <= 1 {
            return true;
        }
        [3, 5, 7].into_iter().any(|base| {
            let mut remaining = group;
            while remaining.is_multiple_of(base) {
                remaining /= base;
            }
            remaining == 1
        })
    }

    fn inode_table_blocks(&self) -> u32 {
        (u32::from(self.inode_size) * self.inodes_per_group).div_ceil(BLOCK_SIZE as u32)
    }

    fn validate_write_geometry(&self) -> Result<(), Error> {
        for (index, group) in self.groups.iter().enumerate() {
            let range = self.group_range(index);
            let table = group.inode_table..group.inode_table + self.inode_table_blocks();
            if !range.contains(&group.block_bitmap)
                || !range.contains(&group.inode_bitmap)
                || group.block_bitmap == group.inode_bitmap
                || !range.contains(&table.start)
                || table.end > range.end
                || table.contains(&group.block_bitmap)
                || table.contains(&group.inode_bitmap)
            {
                return Err(Error::Corrupt);
            }
            if self.has_superblock(index) {
                let end = range.start + 2 + u32::from(self.reserved_gdt_blocks);
                if end > range.end
                    || group.block_bitmap < end
                    || group.inode_bitmap < end
                    || table.start < end
                {
                    return Err(Error::Corrupt);
                }
            }
        }
        Ok(())
    }

    fn is_metadata(&self, number: u32) -> bool {
        self.groups.iter().enumerate().any(|(index, group)| {
            number == group.block_bitmap
                || number == group.inode_bitmap
                || (group.inode_table..group.inode_table + self.inode_table_blocks())
                    .contains(&number)
                || (self.has_superblock(index)
                    && (self.group_range(index).start
                        ..self.group_range(index).start + 2 + u32::from(self.reserved_gdt_blocks))
                        .contains(&number))
        })
    }

    pub fn write(
        &mut self,
        number: InodeNumber,
        offset: u64,
        source: &[u8],
    ) -> Result<usize, Error> {
        let mut inode = self.inode(number)?;
        if !inode.is_file() {
            return Err(Error::NotFile);
        }
        if inode.flags != 0 {
            return Err(Error::Unsupported);
        }
        let end = offset
            .checked_add(source.len() as u64)
            .ok_or(Error::FileTooLarge)?;
        if end > u64::from(MAX_FILE_BLOCKS) * BLOCK_SIZE as u64 {
            return Err(Error::FileTooLarge);
        }
        if source.is_empty() {
            return Ok(0);
        }
        self.validate_mutation()?;
        let mut mutation = Mutation::new(self);
        mutation.zero_gap(&inode, offset)?;
        let mut copied = 0;
        while copied < source.len() {
            let position = offset + copied as u64;
            let block =
                mutation.ensure_data_block(&mut inode, (position / BLOCK_SIZE as u64) as u32)?;
            let mut data = mutation.block(block)?;
            let within = position as usize % BLOCK_SIZE;
            let take = (BLOCK_SIZE - within).min(source.len() - copied);
            data[within..within + take].copy_from_slice(&source[copied..copied + take]);
            mutation.stage(block, data)?;
            copied += take;
        }
        inode.size = inode.size.max(end);
        mutation.store_inode(&inode)?;
        mutation.commit()?;
        self.record_change(
            number,
            hunde::io::FileChangeEvents(hunde::protocol::IO_FILE_CHANGE_WRITE),
        );
        Ok(copied)
    }

    fn validate_mutation(&mut self) -> Result<(), Error> {
        self.validate_write_geometry()?;
        let superblock = self.block(1)?;
        if read_u32(&superblock, 28)? != 0 {
            return Err(Error::Unsupported);
        }
        if read_u32(&superblock, 76)? == 1 && read_u32(&superblock, 92)? & !0x38 != 0 {
            return Err(Error::Unsupported);
        }
        Ok(())
    }

    pub fn create_file(
        &mut self,
        parent: InodeNumber,
        name: &[u8],
        permissions: u16,
        user: u16,
        group: u16,
    ) -> Result<Inode, Error> {
        if permissions & !0o7777 != 0 {
            return Err(Error::Unsupported);
        }
        self.create(parent, name, REGULAR | permissions, user, group)
    }

    pub fn mkdir(
        &mut self,
        parent: InodeNumber,
        name: &[u8],
        permissions: u16,
        user: u16,
        group: u16,
    ) -> Result<Inode, Error> {
        if permissions & !0o7777 != 0 {
            return Err(Error::Unsupported);
        }
        self.create(parent, name, DIRECTORY | permissions, user, group)
    }

    fn create(
        &mut self,
        parent: InodeNumber,
        name: &[u8],
        mode: u16,
        user: u16,
        group: u16,
    ) -> Result<Inode, Error> {
        validate_name(name)?;
        let mut directory = self.writable_directory(parent)?;
        if self.entries(parent)?.iter().any(|entry| entry.name == name) {
            return Err(Error::AlreadyExists);
        }
        if mode & TYPE_MASK == DIRECTORY {
            directory.links = directory.links.checked_add(1).ok_or(Error::TooManyLinks)?;
        }
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(parent)?;
        let number = mutation.allocate_inode(mode & TYPE_MASK == DIRECTORY)?;
        let mut inode = Inode::new(number, mode, user, group);
        mutation.clear_inode(number)?;
        if inode.is_directory() {
            let block = mutation.ensure_data_block(&mut inode, 0)?;
            let mut data = [0; BLOCK_SIZE];
            mutation.directory_record(&mut data, 0..12, b".", number, DIRECTORY);
            mutation.directory_record(&mut data, 12..BLOCK_SIZE, b"..", parent, DIRECTORY);
            mutation.stage(block, data)?;
            inode.size = BLOCK_SIZE as u64;
        }
        mutation.insert_entry(&mut directory, name, &inode)?;
        mutation.store_inode(&inode)?;
        mutation.store_inode(&directory)?;
        mutation.commit()?;
        Ok(inode)
    }

    fn writable_directory(&mut self, number: InodeNumber) -> Result<Inode, Error> {
        let directory = self.inode(number)?;
        if !directory.is_directory() {
            return Err(Error::NotDirectory);
        }
        if directory.flags != 0 {
            return Err(Error::Unsupported);
        }
        if directory.links == 0
            || directory.size == 0
            || !directory.size.is_multiple_of(BLOCK_SIZE as u64)
        {
            return Err(Error::Corrupt);
        }
        self.validate_mutation()?;
        Ok(directory)
    }

    pub fn unlink(&mut self, parent: InodeNumber, name: &[u8]) -> Result<(), Error> {
        self.remove(parent, name, false)
    }

    pub fn link(
        &mut self,
        parent: InodeNumber,
        name: &[u8],
        target: InodeNumber,
    ) -> Result<(), Error> {
        validate_name(name)?;
        let mut directory = self.writable_directory(parent)?;
        if self.entries(parent)?.iter().any(|entry| entry.name == name) {
            return Err(Error::AlreadyExists);
        }
        let mut inode = self.inode(target)?;
        if inode.is_directory() {
            return Err(Error::IsDirectory);
        }
        if !inode.is_file() {
            return Err(Error::Unsupported);
        }
        if inode.links == 0 {
            return Err(Error::Corrupt);
        }
        inode.links = inode.links.checked_add(1).ok_or(Error::TooManyLinks)?;
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(parent)?;
        mutation.check_inode_allocated(target)?;
        let superblock = mutation.block(1)?;
        if target.0 < mutation.volume.first_inode(&superblock)? {
            return Err(Error::Corrupt);
        }
        let blocks = mutation.inode_blocks(&inode)?;
        let parent_blocks = mutation.inode_blocks(&directory)?;
        if !blocks.is_disjoint(&parent_blocks) {
            return Err(Error::Corrupt);
        }
        mutation.insert_entry(&mut directory, name, &inode)?;
        mutation.store_inode(&inode)?;
        mutation.store_inode(&directory)?;
        mutation.commit()
    }

    pub fn rmdir(&mut self, parent: InodeNumber, name: &[u8]) -> Result<(), Error> {
        self.remove(parent, name, true)
    }

    pub(super) fn validate_open_inode(&mut self, inode: &Inode) -> Result<(), Error> {
        self.validate_mutation()?;
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(inode.number)?;
        mutation.inode_blocks(inode)?;
        Ok(())
    }

    pub(super) fn reclaim_unlinked(&mut self, number: InodeNumber) -> Result<(), Error> {
        let inode = self.inode(number)?;
        if inode.links != 0 {
            return Ok(());
        }
        self.validate_mutation()?;
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(number)?;
        for block in mutation.inode_blocks(&inode)? {
            mutation.release_block(block)?;
        }
        mutation.release_inode(&inode)?;
        mutation.commit()
    }

    pub fn truncate(&mut self, number: InodeNumber) -> Result<(), Error> {
        let mut inode = self.inode(number)?;
        if !inode.is_file() {
            return Err(Error::NotFile);
        }
        if inode.flags != 0 {
            return Err(Error::Unsupported);
        }
        self.validate_mutation()?;
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(number)?;
        for block in mutation.inode_blocks(&inode)? {
            mutation.release_block(block)?;
        }
        inode.blocks.fill(0);
        inode.size = 0;
        inode.sectors = 0;
        mutation.store_inode(&inode)?;
        mutation.commit()?;
        self.record_change(
            number,
            hunde::io::FileChangeEvents(hunde::protocol::IO_FILE_CHANGE_TRUNCATE),
        );
        Ok(())
    }

    fn remove(&mut self, parent: InodeNumber, name: &[u8], directory: bool) -> Result<(), Error> {
        validate_name(name)?;
        let mut parent_inode = self.writable_directory(parent)?;
        let entries = self.entries(parent)?;
        let mut matching = entries.iter().filter(|entry| entry.name == name);
        let number = matching.next().ok_or(Error::NotFound)?.inode;
        if matching.next().is_some() || number == parent || number == InodeNumber(2) {
            return Err(Error::Corrupt);
        }
        let mut inode = self.inode(number)?;
        if directory {
            if !inode.is_directory() {
                return Err(Error::NotDirectory);
            }
            let children = self.entries(number)?;
            if children
                .iter()
                .any(|entry| entry.name != b"." && entry.name != b"..")
            {
                return Err(Error::NotEmpty);
            }
            if children.len() != 2
                || !children
                    .iter()
                    .any(|entry| entry.name == b"." && entry.inode == number)
                || !children
                    .iter()
                    .any(|entry| entry.name == b".." && entry.inode == parent)
                || inode.links != 2
                || parent_inode.links < 3
                || inode.size == 0
                || !inode.size.is_multiple_of(BLOCK_SIZE as u64)
            {
                return Err(Error::Corrupt);
            }
            parent_inode.links -= 1;
        } else if inode.is_directory() {
            return Err(Error::IsDirectory);
        } else if !inode.is_file() {
            return Err(Error::Unsupported);
        }
        if inode.links == 0 {
            return Err(Error::Corrupt);
        }
        let retained = !directory && self.has_open_file(number);
        let mut mutation = Mutation::new(self);
        mutation.check_inode_allocated(parent)?;
        mutation.check_inode_allocated(number)?;
        let blocks = mutation.inode_blocks(&inode)?;
        let parent_blocks = mutation.inode_blocks(&parent_inode)?;
        if !blocks.is_disjoint(&parent_blocks) {
            return Err(Error::Corrupt);
        }
        mutation.remove_entry(&parent_inode, name)?;
        if directory || (inode.links == 1 && !retained) {
            for block in blocks {
                mutation.release_block(block)?;
            }
            mutation.release_inode(&inode)?;
        } else {
            inode.links -= 1;
            mutation.store_inode(&inode)?;
        }
        mutation.store_inode(&parent_inode)?;
        mutation.commit()?;
        if retained && inode.links == 0 {
            self.record_change(
                number,
                hunde::io::FileChangeEvents(hunde::protocol::IO_FILE_CHANGE_DELETE),
            );
        }
        Ok(())
    }
}

impl<'a, D: Device> Mutation<'a, D> {
    fn new(volume: &'a mut Volume<D>) -> Self {
        Self {
            volume,
            changes: BTreeMap::new(),
        }
    }

    fn block(&mut self, number: u32) -> Result<[u8; BLOCK_SIZE], Error> {
        match self.changes.get(&number) {
            Some(change) => Ok(change.updated),
            None => self.volume.block(number),
        }
    }

    fn stage(&mut self, number: u32, updated: [u8; BLOCK_SIZE]) -> Result<(), Error> {
        match self.changes.entry(number) {
            Entry::Occupied(mut entry) => entry.get_mut().updated = updated,
            Entry::Vacant(entry) => {
                entry.insert(Change {
                    original: self.volume.block(number)?,
                    updated,
                });
            }
        }
        Ok(())
    }

    fn check_inode_allocated(&mut self, number: InodeNumber) -> Result<(), Error> {
        let index = number.0 - 1;
        let group = (index / self.volume.inodes_per_group) as usize;
        let bitmap = self.block(self.volume.groups[group].inode_bitmap)?;
        let bit = index % self.volume.inodes_per_group;
        if bitmap[bit as usize / 8] & (1 << (bit % 8)) == 0 {
            return Err(Error::Corrupt);
        }
        Ok(())
    }

    fn allocate_inode(&mut self, directory: bool) -> Result<InodeNumber, Error> {
        let mut superblock = self.block(1)?;
        let first = self.volume.first_inode(&superblock)?;
        let free = read_u32(&superblock, 16)?;
        if free > self.volume.inodes {
            return Err(Error::Corrupt);
        }
        if free == 0 {
            return Err(Error::NoSpace);
        }
        let mut descriptors = self.block(2)?;
        for index in 0..self.volume.groups.len() {
            let start = index as u32 * self.volume.inodes_per_group;
            let count = self
                .volume
                .inodes
                .saturating_sub(start)
                .min(self.volume.inodes_per_group);
            let group_free = read_u16(&descriptors, index * 32 + 14)?;
            if u32::from(group_free) > count {
                return Err(Error::Corrupt);
            }
            if group_free == 0 {
                continue;
            }
            let bitmap_number = self.volume.groups[index].inode_bitmap;
            let mut bitmap = self.block(bitmap_number)?;
            for bit in 0..count {
                if bitmap[bit as usize / 8] & (1 << (bit % 8)) != 0 {
                    continue;
                }
                let number = start + bit + 1;
                if number < first {
                    return Err(Error::Corrupt);
                }
                bitmap[bit as usize / 8] |= 1 << (bit % 8);
                store_u32(&mut superblock, 16, free - 1);
                store_u16(&mut descriptors, index * 32 + 14, group_free - 1);
                if directory {
                    let used = read_u16(&descriptors, index * 32 + 16)?;
                    if u32::from(used) > count - u32::from(group_free) {
                        return Err(Error::Corrupt);
                    }
                    store_u16(&mut descriptors, index * 32 + 16, used + 1);
                }
                self.stage(bitmap_number, bitmap)?;
                self.stage(1, superblock)?;
                self.stage(2, descriptors)?;
                return Ok(InodeNumber(number));
            }
            return Err(Error::Corrupt);
        }
        Err(Error::Corrupt)
    }

    fn clear_inode(&mut self, number: InodeNumber) -> Result<(), Error> {
        let InodeLocation { block, offset } = self.volume.inode_location(number)?;
        let mut data = self.block(block)?;
        data[offset..offset + self.volume.inode_size as usize].fill(0);
        self.stage(block, data)
    }

    fn inode_blocks(&mut self, inode: &Inode) -> Result<BTreeSet<u32>, Error> {
        let location = self.volume.inode_location(inode.number)?;
        let record = self.block(location.block)?;
        if inode.flags != 0
            || inode.blocks[14] != 0
            || read_u32(&record, location.offset + 104)? != 0
            || read_u32(&record, location.offset + 112)? != 0
        {
            return Err(Error::Unsupported);
        }
        let mut blocks = BTreeSet::new();
        for number in &inode.blocks[..DIRECT_BLOCKS as usize] {
            self.collect_blocks(*number, 0, &mut blocks)?;
        }
        self.collect_blocks(inode.blocks[12], 1, &mut blocks)?;
        self.collect_blocks(inode.blocks[13], 2, &mut blocks)?;
        if blocks.len() as u32 * 2 != inode.sectors {
            return Err(Error::Corrupt);
        }
        Ok(blocks)
    }

    fn collect_blocks(
        &mut self,
        number: u32,
        depth: u8,
        blocks: &mut BTreeSet<u32>,
    ) -> Result<(), Error> {
        if number == 0 {
            return Ok(());
        }
        self.check_data_block(number)?;
        if !blocks.insert(number) {
            return Err(Error::Corrupt);
        }
        if depth != 0 {
            let data = self.block(number)?;
            for offset in (0..BLOCK_SIZE).step_by(4) {
                self.collect_blocks(read_u32(&data, offset)?, depth - 1, blocks)?;
            }
        }
        Ok(())
    }

    fn release_block(&mut self, number: u32) -> Result<(), Error> {
        self.check_data_block(number)?;
        let group = ((number - 1) / self.volume.blocks_per_group) as usize;
        let range = self.volume.group_range(group);
        let mut descriptors = self.block(2)?;
        let group_free = read_u16(&descriptors, group * 32 + 12)?;
        let mut superblock = self.block(1)?;
        let free = read_u32(&superblock, 12)?;
        if u32::from(group_free) >= range.len() as u32 || free >= self.volume.blocks - 1 {
            return Err(Error::Corrupt);
        }
        let bitmap_number = self.volume.groups[group].block_bitmap;
        let mut bitmap = self.block(bitmap_number)?;
        let bit = (number - 1) % self.volume.blocks_per_group;
        bitmap[bit as usize / 8] &= !(1 << (bit % 8));
        store_u16(&mut descriptors, group * 32 + 12, group_free + 1);
        store_u32(&mut superblock, 12, free + 1);
        self.stage(bitmap_number, bitmap)?;
        self.stage(1, superblock)?;
        self.stage(2, descriptors)
    }

    fn release_inode(&mut self, inode: &Inode) -> Result<(), Error> {
        let mut superblock = self.block(1)?;
        let first = self.volume.first_inode(&superblock)?;
        if inode.number.0 < first {
            return Err(Error::Corrupt);
        }
        let free = read_u32(&superblock, 16)?;
        let index = inode.number.0 - 1;
        let group = (index / self.volume.inodes_per_group) as usize;
        let count = self
            .volume
            .inodes
            .saturating_sub(group as u32 * self.volume.inodes_per_group)
            .min(self.volume.inodes_per_group);
        let mut descriptors = self.block(2)?;
        let group_free = read_u16(&descriptors, group * 32 + 14)?;
        if u32::from(group_free) >= count || free >= self.volume.inodes {
            return Err(Error::Corrupt);
        }
        if inode.is_directory() {
            let used = read_u16(&descriptors, group * 32 + 16)?;
            if used == 0 || u32::from(used) > count - u32::from(group_free) {
                return Err(Error::Corrupt);
            }
            store_u16(&mut descriptors, group * 32 + 16, used - 1);
        }
        let bitmap_number = self.volume.groups[group].inode_bitmap;
        let mut bitmap = self.block(bitmap_number)?;
        let bit = index % self.volume.inodes_per_group;
        bitmap[bit as usize / 8] &= !(1 << (bit % 8));
        store_u16(&mut descriptors, group * 32 + 14, group_free + 1);
        store_u32(&mut superblock, 16, free + 1);
        self.stage(bitmap_number, bitmap)?;
        self.stage(1, superblock)?;
        self.stage(2, descriptors)?;
        self.clear_inode(inode.number)
    }

    fn remove_entry(&mut self, directory: &Inode, name: &[u8]) -> Result<(), Error> {
        for index in 0..(directory.size / BLOCK_SIZE as u64) as u32 {
            let block = self.volume.data_block(directory, index)?;
            self.check_data_block(block)?;
            let mut data = self.block(block)?;
            let mut offset = 0;
            while offset < BLOCK_SIZE {
                let length = read_u16(&data, offset + 4)? as usize;
                let name_length = if self.volume.directory_types {
                    data[offset + 6] as usize
                } else {
                    read_u16(&data, offset + 6)? as usize
                };
                if read_u32(&data, offset)? != 0
                    && &data[offset + 8..offset + 8 + name_length] == name
                {
                    data[offset..offset + length].fill(0);
                    store_u16(&mut data, offset + 4, length as u16);
                    return self.stage(block, data);
                }
                offset += length;
            }
        }
        Err(Error::Corrupt)
    }

    fn directory_record(
        &self,
        data: &mut [u8; BLOCK_SIZE],
        range: Range<usize>,
        name: &[u8],
        number: InodeNumber,
        kind: u16,
    ) {
        let offset = range.start;
        let length = range.len();
        data[range].fill(0);
        store_u32(data, offset, number.0);
        store_u16(data, offset + 4, length as u16);
        store_u16(data, offset + 6, name.len() as u16);
        if self.volume.directory_types {
            data[offset + 7] = if kind == DIRECTORY { 2 } else { 1 };
        }
        data[offset + 8..offset + 8 + name.len()].copy_from_slice(name);
    }

    fn insert_entry(
        &mut self,
        directory: &mut Inode,
        name: &[u8],
        inode: &Inode,
    ) -> Result<(), Error> {
        let needed = (8 + name.len()).next_multiple_of(4);
        for index in 0..(directory.size / BLOCK_SIZE as u64) as u32 {
            let block = self.volume.data_block(directory, index)?;
            self.check_data_block(block)?;
            let mut data = self.block(block)?;
            let mut offset = 0;
            while offset < BLOCK_SIZE {
                let length = read_u16(&data, offset + 4)? as usize;
                let number = read_u32(&data, offset)?;
                let name_length = if self.volume.directory_types {
                    data[offset + 6] as usize
                } else {
                    read_u16(&data, offset + 6)? as usize
                };
                let occupied = if number == 0 {
                    0
                } else {
                    (8 + name_length).next_multiple_of(4)
                };
                if length - occupied >= needed {
                    if occupied != 0 {
                        store_u16(&mut data, offset + 4, occupied as u16);
                    }
                    self.directory_record(
                        &mut data,
                        offset + occupied..offset + length,
                        name,
                        inode.number,
                        inode.mode & TYPE_MASK,
                    );
                    return self.stage(block, data);
                }
                offset += length;
            }
        }
        let index = (directory.size / BLOCK_SIZE as u64) as u32;
        let block = self.ensure_data_block(directory, index)?;
        let mut data = [0; BLOCK_SIZE];
        self.directory_record(
            &mut data,
            0..BLOCK_SIZE,
            name,
            inode.number,
            inode.mode & TYPE_MASK,
        );
        self.stage(block, data)?;
        directory.size += BLOCK_SIZE as u64;
        Ok(())
    }

    fn check_data_block(&mut self, number: u32) -> Result<(), Error> {
        if number == 0 || number >= self.volume.blocks || self.volume.is_metadata(number) {
            return Err(Error::Corrupt);
        }
        let group = (number - 1) / self.volume.blocks_per_group;
        let bitmap = self.block(self.volume.groups[group as usize].block_bitmap)?;
        let bit = (number - 1) % self.volume.blocks_per_group;
        if bitmap[bit as usize / 8] & (1 << (bit % 8)) == 0 {
            return Err(Error::Corrupt);
        }
        Ok(())
    }

    fn allocate(&mut self, inode: &mut Inode) -> Result<u32, Error> {
        let mut superblock = self.block(1)?;
        let free = read_u32(&superblock, 12)?;
        if free == 0 {
            return Err(Error::NoSpace);
        }
        if free > self.volume.blocks - 1 {
            return Err(Error::Corrupt);
        }
        let mut descriptors = self.block(2)?;
        for index in 0..self.volume.groups.len() {
            let range = self.volume.group_range(index);
            let group_free = read_u16(&descriptors, index * 32 + 12)?;
            if u32::from(group_free) > range.end - range.start {
                return Err(Error::Corrupt);
            }
            if group_free == 0 {
                continue;
            }
            let bitmap_number = self.volume.groups[index].block_bitmap;
            let mut bitmap = self.block(bitmap_number)?;
            for bit in 0..range.end - range.start {
                if bitmap[bit as usize / 8] & (1 << (bit % 8)) != 0 {
                    continue;
                }
                let number = range.start + bit;
                if self.volume.is_metadata(number) {
                    return Err(Error::Corrupt);
                }
                bitmap[bit as usize / 8] |= 1 << (bit % 8);
                store_u32(&mut superblock, 12, free - 1);
                store_u16(&mut descriptors, index * 32 + 12, group_free - 1);
                self.stage(bitmap_number, bitmap)?;
                self.stage(1, superblock)?;
                self.stage(2, descriptors)?;
                self.stage(number, [0; BLOCK_SIZE])?;
                inode.sectors = inode.sectors.checked_add(2).ok_or(Error::Corrupt)?;
                return Ok(number);
            }
            return Err(Error::Corrupt);
        }
        Err(Error::NoSpace)
    }

    fn pointer(&mut self, table: u32, index: u32) -> Result<u32, Error> {
        self.check_data_block(table)?;
        read_u32(&self.block(table)?, index as usize * 4)
    }

    fn ensure_pointer(&mut self, inode: &mut Inode, table: u32, index: u32) -> Result<u32, Error> {
        let existing = self.pointer(table, index)?;
        if existing != 0 {
            self.check_data_block(existing)?;
            return Ok(existing);
        }
        let allocated = self.allocate(inode)?;
        let mut data = self.block(table)?;
        store_u32(&mut data, index as usize * 4, allocated);
        self.stage(table, data)?;
        Ok(allocated)
    }

    fn ensure_data_block(&mut self, inode: &mut Inode, index: u32) -> Result<u32, Error> {
        if index < DIRECT_BLOCKS {
            let existing = inode.blocks[index as usize];
            if existing != 0 {
                self.check_data_block(existing)?;
                return Ok(existing);
            }
            let allocated = self.allocate(inode)?;
            inode.blocks[index as usize] = allocated;
            return Ok(allocated);
        }
        let index = index - DIRECT_BLOCKS;
        if index < POINTERS_PER_BLOCK {
            if inode.blocks[12] == 0 {
                inode.blocks[12] = self.allocate(inode)?;
            }
            return self.ensure_pointer(inode, inode.blocks[12], index);
        }
        let index = index - POINTERS_PER_BLOCK;
        if index >= POINTERS_PER_BLOCK * POINTERS_PER_BLOCK {
            return Err(Error::FileTooLarge);
        }
        if inode.blocks[13] == 0 {
            inode.blocks[13] = self.allocate(inode)?;
        }
        let table = self.ensure_pointer(inode, inode.blocks[13], index / POINTERS_PER_BLOCK)?;
        self.ensure_pointer(inode, table, index % POINTERS_PER_BLOCK)
    }

    fn zero_gap(&mut self, inode: &Inode, offset: u64) -> Result<(), Error> {
        let mut position = inode.size;
        while position < offset {
            let block = self
                .volume
                .data_block(inode, (position / BLOCK_SIZE as u64) as u32)?;
            let within = position as usize % BLOCK_SIZE;
            let take = (BLOCK_SIZE - within).min((offset - position) as usize);
            if block != 0 {
                self.check_data_block(block)?;
                let mut data = self.block(block)?;
                data[within..within + take].fill(0);
                self.stage(block, data)?;
            }
            position += take as u64;
        }
        Ok(())
    }

    fn store_inode(&mut self, inode: &Inode) -> Result<(), Error> {
        let InodeLocation { block, offset } = self.volume.inode_location(inode.number)?;
        let mut data = self.block(block)?;
        store_u16(&mut data, offset, inode.mode);
        store_u16(&mut data, offset + 2, inode.user);
        store_u16(&mut data, offset + 24, inode.group);
        store_u16(&mut data, offset + 26, inode.links);
        store_u32(&mut data, offset + 4, inode.size as u32);
        store_u32(&mut data, offset + 28, inode.sectors);
        for (index, number) in inode.blocks.iter().enumerate() {
            store_u32(&mut data, offset + 40 + index * 4, *number);
        }
        self.stage(block, data)
    }

    fn commit(self) -> Result<(), Error> {
        for (index, (number, change)) in self.changes.iter().enumerate() {
            if let Err(error) = self
                .volume
                .device
                .write(Sector(u64::from(*number) * 2), &change.updated)
            {
                let mut restored = true;
                for (number, change) in self.changes.iter().take(index + 1).rev() {
                    if self
                        .volume
                        .device
                        .write(Sector(u64::from(*number) * 2), &change.original)
                        .is_err()
                    {
                        restored = false;
                    }
                }
                if !restored {
                    self.volume.recovery_required = true;
                    return Err(Error::RecoveryRequired);
                }
                return Err(Error::Device(error));
            }
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::Disk;

    const FILE: InodeNumber = InodeNumber(12);
    const ROOT: InodeNumber = InodeNumber(2);

    #[test]
    fn links_share_metadata_and_writes_and_only_final_unlink_reclaims() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let directory = volume.mkdir(ROOT, b"dir", 0o755, 0, 0).unwrap();
        let allocations = volume.device.bytes[..5 * BLOCK_SIZE].to_vec();
        assert_eq!(volume.link(directory.number, b"alias", FILE), Ok(()));
        let inode = volume.lookup(b"/dir/alias").unwrap();
        assert_eq!(inode.number, FILE);
        assert_eq!(inode.links, 2);
        assert_eq!(volume.lookup(b"/file"), Ok(inode));
        assert_eq!(volume.device.bytes[..5 * BLOCK_SIZE], allocations);
        assert_eq!(volume.write(FILE, 0, b"world"), Ok(5));
        let mut data = [0; 5];
        let mut remounted = Volume::mount(volume.device).unwrap();
        assert_eq!(remounted.lookup(b"/dir/alias").unwrap().links, 2);
        assert_eq!(remounted.read(FILE, 0, &mut data), Ok(5));
        assert_eq!(&data, b"world");
        remounted.unlink(ROOT, b"file").unwrap();
        assert_eq!(remounted.lookup(b"/dir/alias").unwrap().links, 1);
        assert_eq!(remounted.device.bytes[..5 * BLOCK_SIZE], allocations);
        remounted.unlink(directory.number, b"alias").unwrap();
        assert_eq!(read_u32(&remounted.device.bytes, 1036), Ok(53));
        assert_eq!(read_u32(&remounted.device.bytes, 1040), Ok(20));
        remounted.rmdir(ROOT, b"dir").unwrap();
    }

    #[test]
    fn hard_links_validate_names_types_duplicates_flags_and_counts_without_writes() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let before = volume.device.bytes.clone();
        for name in [b"".as_slice(), b".", b"..", b"a/b", b"a\0b", &[b'x'; 256]] {
            assert_eq!(volume.link(ROOT, name, FILE), Err(Error::InvalidPath));
        }
        assert_eq!(volume.link(ROOT, b"file", FILE), Err(Error::AlreadyExists));
        assert_eq!(volume.link(ROOT, b"root", ROOT), Err(Error::IsDirectory));
        assert_eq!(volume.link(FILE, b"alias", FILE), Err(Error::NotDirectory));
        assert_eq!(
            volume.link(ROOT, b"alias", InodeNumber(0)),
            Err(Error::Corrupt)
        );
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
        for (offset, value, expected) in [
            (Disk::inode_offset(12) + 26, 0, Error::Corrupt),
            (
                Disk::inode_offset(12) + 26,
                u16::MAX as u32,
                Error::TooManyLinks,
            ),
            (Disk::inode_offset(12) + 32, 16, Error::Unsupported),
            (Disk::inode_offset(12), 0xa000, Error::Unsupported),
            (Disk::inode_offset(12) + 40, 9, Error::Corrupt),
            (Disk::inode_offset(12) + 104, 11, Error::Unsupported),
            (1108, 13, Error::Corrupt),
        ] {
            let mut disk = Disk::new();
            disk.word(offset, value);
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                volume.link(ROOT, b"alias", FILE),
                Err(expected),
                "offset {offset}"
            );
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
        for bitmap in [3, 4] {
            let mut disk = Disk::new();
            if bitmap == 3 {
                disk.bytes[3 * BLOCK_SIZE + 1] &= !2;
            } else {
                disk.bytes[4 * BLOCK_SIZE + 1] &= !8;
            }
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(volume.link(ROOT, b"alias", FILE), Err(Error::Corrupt));
            assert_eq!(volume.device.bytes, before);
        }
    }

    #[test]
    fn hard_links_grow_directories_without_allocating_inodes_and_fail_without_space() {
        let mut volume = Volume::mount(full_directory_disk()).unwrap();
        let before = volume.device.bytes.clone();
        assert_eq!(volume.link(ROOT, &[b'n'; 255], FILE), Ok(()));
        assert_eq!(volume.inode(ROOT).unwrap().size, 2 * BLOCK_SIZE as u64);
        assert_eq!(volume.inode(FILE).unwrap().links, 2);
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(20));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(52));
        assert_eq!(
            &volume.device.bytes[4 * BLOCK_SIZE..5 * BLOCK_SIZE],
            &before[4 * BLOCK_SIZE..5 * BLOCK_SIZE]
        );
        let mut disk = full_directory_disk();
        disk.word(1036, 0);
        let before = disk.bytes.clone();
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.link(ROOT, &[b'n'; 255], FILE), Err(Error::NoSpace));
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn hard_links_support_legacy_records_and_existing_space_on_a_full_device() {
        for revision in [0, 1] {
            let mut disk = Disk::new();
            disk.word(1100, revision);
            disk.word(1120, 0);
            disk.word(1036, 0);
            disk.word(1040, 0);
            for offset in [0, 12, 24] {
                disk.bytes[9 * BLOCK_SIZE + offset + 7] = 0;
            }
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(volume.link(ROOT, b"alias", FILE), Ok(()));
            assert_eq!(volume.lookup(b"/alias").unwrap().number, FILE);
            assert_eq!(volume.inode(FILE).unwrap().links, 2);
            assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(0));
            assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(0));
        }
    }

    #[test]
    fn hard_link_staging_and_partial_commit_failures_restore_all_bytes() {
        for growth in [false, true] {
            let prepare = || {
                Volume::mount(if growth {
                    full_directory_disk()
                } else {
                    Disk::new()
                })
                .unwrap()
            };
            let name = if growth { &[b'n'; 255][..] } else { b"alias" };
            let mut control = prepare();
            let first = control.device.reads + 1;
            control.link(ROOT, name, FILE).unwrap();
            for fail_at in first..=control.device.reads {
                let mut volume = prepare();
                let before = volume.device.bytes.clone();
                volume.device.fail_reads.push(fail_at);
                assert!(
                    matches!(volume.link(ROOT, name, FILE), Err(Error::Device(_))),
                    "read {fail_at}"
                );
                assert_eq!(volume.device.bytes, before);
                assert_eq!(volume.device.writes, 0);
            }
            for fail_at in 1..=control.device.writes {
                let mut volume = prepare();
                let before = volume.device.bytes.clone();
                volume.device.fail_writes.push(fail_at);
                assert!(
                    matches!(volume.link(ROOT, name, FILE), Err(Error::Device(_))),
                    "write {fail_at}"
                );
                assert_eq!(volume.device.bytes, before);
                assert_eq!(volume.inode(FILE).unwrap().links, 1);
                assert_eq!(volume.link(ROOT, name, FILE), Ok(()));
            }
        }
    }

    #[test]
    fn failed_hard_link_rollback_disables_volume_io() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume.device.fail_writes.extend([2, 3]);
        assert_eq!(
            volume.link(ROOT, b"alias", FILE),
            Err(Error::RecoveryRequired)
        );
        let writes = volume.device.writes;
        assert_eq!(
            volume.link(ROOT, b"alias", FILE),
            Err(Error::RecoveryRequired)
        );
        assert_eq!(volume.lookup(b"/file"), Err(Error::RecoveryRequired));
        assert_eq!(volume.device.writes, writes);
    }

    #[test]
    fn truncation_reclaims_sparse_blocks_without_replacing_the_inode() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume.link(ROOT, b"alias", FILE).unwrap();
        for logical in [11, 12, 267, 268, 524] {
            volume
                .write(FILE, logical * BLOCK_SIZE as u64, b"data")
                .unwrap();
        }
        let original = volume.inode(FILE).unwrap();
        let free = read_u32(&volume.device.bytes, 1036).unwrap();
        let free_inodes = read_u32(&volume.device.bytes, 1040).unwrap();
        volume.truncate(FILE).unwrap();
        let inode = volume.lookup(b"/alias").unwrap();
        assert_eq!(
            (
                inode.number,
                inode.mode,
                inode.user,
                inode.group,
                inode.links
            ),
            (
                original.number,
                original.mode,
                original.user,
                original.group,
                original.links
            )
        );
        assert_eq!((inode.size, inode.sectors, inode.blocks), (0, 0, [0; 15]));
        assert_eq!(
            read_u32(&volume.device.bytes, 1036),
            Ok(free + original.sectors / 2)
        );
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(free_inodes));
        volume.write(FILE, 7, b"fresh").unwrap();
        let mut remounted = Volume::mount(volume.device).unwrap();
        let mut data = [255; 12];
        assert_eq!(remounted.read(FILE, 0, &mut data), Ok(12));
        assert_eq!(&data, b"\0\0\0\0\0\0\0fresh");
        assert_eq!(remounted.lookup(b"/file").unwrap().number, FILE);
    }

    #[test]
    fn failed_truncation_rolls_back_every_commit_write() {
        let mut control = Volume::mount(Disk::new()).unwrap();
        control.truncate(FILE).unwrap();
        for fail_at in 1..=control.device.writes {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let before = volume.device.bytes.clone();
            volume.device.fail_writes.push(fail_at);
            assert!(
                matches!(volume.truncate(FILE), Err(Error::Device(_))),
                "write {fail_at}"
            );
            assert_eq!(volume.device.bytes, before);
            volume.truncate(FILE).unwrap();
        }
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume.device.fail_writes.extend([2, 3]);
        assert_eq!(volume.truncate(FILE), Err(Error::RecoveryRequired));
        assert_eq!(volume.lookup(b"/file"), Err(Error::RecoveryRequired));
    }

    #[test]
    fn unlink_reclaims_sparse_direct_single_and_double_indirect_allocations() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        for logical in [11, 12, 267, 268, 524] {
            volume
                .write(FILE, logical * BLOCK_SIZE as u64, b"data")
                .unwrap();
        }
        let inode = volume.inode(FILE).unwrap();
        let free = read_u32(&volume.device.bytes, 1036).unwrap();
        assert_eq!(volume.unlink(ROOT, b"file"), Ok(()));
        assert_eq!(volume.lookup(b"/file"), Err(Error::NotFound));
        assert_eq!(
            read_u32(&volume.device.bytes, 1036),
            Ok(free + inode.sectors / 2)
        );
        assert_eq!(
            read_u16(&volume.device.bytes, 2060),
            Ok((free + inode.sectors / 2) as u16)
        );
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(21));
        assert_eq!(read_u16(&volume.device.bytes, 2062), Ok(21));
        assert_eq!(volume.device.bytes[4 * BLOCK_SIZE + 1] & 8, 0);
        assert!(
            volume.device.bytes[Disk::inode_offset(12)..Disk::inode_offset(13)]
                .iter()
                .all(|byte| *byte == 0)
        );
        let created = volume.create_file(ROOT, b"reused", 0o644, 1, 2).unwrap();
        assert_eq!(created.number, FILE);
        volume.write(created.number, 7, b"fresh").unwrap();
        let mut remounted = Volume::mount(volume.device).unwrap();
        let mut data = [255; 12];
        assert_eq!(remounted.read(FILE, 0, &mut data), Ok(12));
        assert_eq!(&data[..7], &[0; 7]);
        assert_eq!(&data[7..], b"fresh");
        assert_eq!(remounted.lookup(b"/file"), Err(Error::NotFound));
    }

    #[test]
    fn unlink_preserves_existing_hard_links_until_the_final_name_is_removed() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(12) + 26, 2);
        disk.half(9 * BLOCK_SIZE + 28, 12);
        disk.entry(9 * BLOCK_SIZE + 36, 12, 988, b"alias");
        let mut volume = Volume::mount(disk).unwrap();
        let before = volume.device.bytes[..5 * BLOCK_SIZE].to_vec();
        assert_eq!(volume.unlink(ROOT, b"file"), Ok(()));
        assert_eq!(volume.lookup(b"/alias").unwrap().links, 1);
        assert_eq!(volume.device.bytes[..5 * BLOCK_SIZE], before);
        let mut data = [0; 5];
        assert_eq!(volume.read(FILE, 0, &mut data), Ok(5));
        assert_eq!(&data, b"hello");
        assert_eq!(volume.unlink(ROOT, b"alias"), Ok(()));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(54));
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(21));
    }

    #[test]
    fn rmdir_requires_an_empty_directory_and_restores_counts_and_parent_links() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let directory = volume.mkdir(ROOT, b"dir", 0o755, 1, 2).unwrap();
        let child = volume
            .create_file(directory.number, b"child", 0o644, 1, 2)
            .unwrap();
        volume
            .write(child.number, 12 * BLOCK_SIZE as u64, b"data")
            .unwrap();
        let before = volume.device.bytes.clone();
        assert_eq!(volume.rmdir(ROOT, b"dir"), Err(Error::NotEmpty));
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.unlink(directory.number, b"child"), Ok(()));
        assert_eq!(volume.rmdir(ROOT, b"dir"), Ok(()));
        assert_eq!(volume.inode(ROOT).unwrap().links, 2);
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(53));
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(20));
        assert_eq!(read_u16(&volume.device.bytes, 2064), Ok(1));
        let mut remounted = Volume::mount(volume.device).unwrap();
        assert_eq!(remounted.lookup(b"/dir"), Err(Error::NotFound));
        assert!(remounted.mkdir(ROOT, b"dir", 0o700, 3, 4).is_ok());
    }

    #[test]
    fn removal_rejects_invalid_names_missing_entries_and_wrong_types() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume.mkdir(ROOT, b"dir", 0o755, 0, 0).unwrap();
        let before = volume.device.bytes.clone();
        for name in [b"".as_slice(), b".", b"..", b"a/b", b"a\0b", &[b'x'; 256]] {
            assert_eq!(volume.unlink(ROOT, name), Err(Error::InvalidPath));
            assert_eq!(volume.rmdir(ROOT, name), Err(Error::InvalidPath));
        }
        assert_eq!(volume.unlink(ROOT, b"absent"), Err(Error::NotFound));
        assert_eq!(volume.rmdir(ROOT, b"absent"), Err(Error::NotFound));
        assert_eq!(volume.unlink(ROOT, b"dir"), Err(Error::IsDirectory));
        assert_eq!(volume.rmdir(ROOT, b"file"), Err(Error::NotDirectory));
        assert_eq!(volume.unlink(FILE, b"child"), Err(Error::NotDirectory));
        assert_eq!(volume.device.bytes, before);
    }

    #[test]
    fn corrupt_and_unsupported_reclamation_metadata_leave_disk_unchanged() {
        for (offset, value, expected) in [
            (Disk::inode_offset(12) + 40, 3, Error::Corrupt),
            (Disk::inode_offset(12) + 44, 10, Error::Corrupt),
            (Disk::inode_offset(12) + 40, 9, Error::Corrupt),
            (Disk::inode_offset(12) + 28, 4, Error::Corrupt),
            (Disk::inode_offset(12) + 26, 0, Error::Corrupt),
            (Disk::inode_offset(12) + 32, 16, Error::Unsupported),
            (Disk::inode_offset(12) + 104, 11, Error::Unsupported),
            (Disk::inode_offset(12) + 112, 11, Error::Unsupported),
            (Disk::inode_offset(12) + 96, 11, Error::Unsupported),
            (1036, 63, Error::Corrupt),
            (1040, 32, Error::Corrupt),
            (1108, 13, Error::Corrupt),
        ] {
            let mut disk = Disk::new();
            disk.word(offset, value);
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                volume.unlink(ROOT, b"file"),
                Err(expected),
                "offset {offset}"
            );
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
    }

    #[test]
    fn removal_staging_and_partial_commit_failures_restore_all_bytes() {
        for directory in [false, true] {
            let prepare = || {
                let mut volume = Volume::mount(Disk::new()).unwrap();
                if directory {
                    volume.mkdir(ROOT, b"target", 0o755, 0, 0).unwrap();
                } else {
                    let inode = volume.create_file(ROOT, b"target", 0o644, 0, 0).unwrap();
                    volume
                        .write(inode.number, 268 * BLOCK_SIZE as u64, b"data")
                        .unwrap();
                }
                volume.device.writes = 0;
                volume
            };
            let remove = |volume: &mut Volume<Disk>| {
                if directory {
                    volume.rmdir(ROOT, b"target")
                } else {
                    volume.unlink(ROOT, b"target")
                }
            };
            let mut control = prepare();
            let first = control.device.reads + 1;
            remove(&mut control).unwrap();
            for fail_at in first..=control.device.reads {
                let mut volume = prepare();
                let before = volume.device.bytes.clone();
                volume.device.fail_reads.push(fail_at);
                assert!(
                    matches!(remove(&mut volume), Err(Error::Device(_))),
                    "read {fail_at}"
                );
                assert_eq!(volume.device.bytes, before);
                assert_eq!(volume.device.writes, 0);
            }
            for fail_at in 1..=control.device.writes {
                let mut volume = prepare();
                let before = volume.device.bytes.clone();
                volume.device.fail_writes.push(fail_at);
                assert!(
                    matches!(remove(&mut volume), Err(Error::Device(_))),
                    "write {fail_at}"
                );
                assert_eq!(volume.device.bytes, before);
                assert!(volume.lookup(b"/target").is_ok());
                assert_eq!(remove(&mut volume), Ok(()));
            }
        }
    }

    #[test]
    fn failed_removal_rollback_disables_volume_io() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume.device.fail_writes.extend([2, 3]);
        assert_eq!(volume.unlink(ROOT, b"file"), Err(Error::RecoveryRequired));
        let writes = volume.device.writes;
        assert_eq!(volume.unlink(ROOT, b"file"), Err(Error::RecoveryRequired));
        assert_eq!(volume.rmdir(ROOT, b"dir"), Err(Error::RecoveryRequired));
        assert_eq!(volume.device.writes, writes);
    }

    #[test]
    fn removal_validates_directory_identity_allocation_and_indirect_aliases() {
        for case in 0..8 {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let inode = volume.mkdir(ROOT, b"dir", 0o755, 0, 0).unwrap();
            let data = inode.blocks[0] as usize * BLOCK_SIZE;
            let record = Disk::inode_offset(inode.number.0 as usize);
            match case {
                0 => volume.device.word(data, 12),
                1 => volume.device.word(data + 12, inode.number.0),
                2 => volume.device.half(record + 26, 3),
                3 => volume.device.half(2064, 0),
                4 => volume.device.bytes[4 * BLOCK_SIZE + 1] &= !16,
                5 => volume.device.bytes[3 * BLOCK_SIZE + 1] &= !4,
                6 => volume.device.half(2062, 32),
                7 => volume.device.half(2060, 63),
                _ => unreachable!(),
            }
            let before = volume.device.bytes.clone();
            let writes = volume.device.writes;
            assert_eq!(
                volume.rmdir(ROOT, b"dir"),
                Err(Error::Corrupt),
                "case {case}"
            );
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, writes);
        }
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume
            .write(FILE, 268 * BLOCK_SIZE as u64, b"data")
            .unwrap();
        let inode = volume.inode(FILE).unwrap();
        let table = inode.blocks[13] as usize * BLOCK_SIZE;
        volume.device.word(table, inode.blocks[13]);
        let before = volume.device.bytes.clone();
        assert_eq!(volume.unlink(ROOT, b"file"), Err(Error::Corrupt));
        assert_eq!(volume.device.bytes, before);
    }

    #[test]
    fn legacy_directory_records_support_unlink_and_rmdir() {
        for revision in [0, 1] {
            let mut disk = Disk::new();
            disk.word(1100, revision);
            disk.word(1120, 0);
            for offset in [0, 12, 24] {
                disk.bytes[9 * BLOCK_SIZE + offset + 7] = 0;
            }
            let mut volume = Volume::mount(disk).unwrap();
            let directory = volume.mkdir(ROOT, b"dir", 0o755, 0, 0).unwrap();
            volume
                .create_file(directory.number, b"child", 0o644, 0, 0)
                .unwrap();
            volume.unlink(directory.number, b"child").unwrap();
            volume.rmdir(ROOT, b"dir").unwrap();
            volume.unlink(ROOT, b"file").unwrap();
            assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(54));
            assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(21));
            assert_eq!(volume.entries(ROOT).unwrap().len(), 2);
        }
    }

    fn full_directory_disk() -> Disk {
        let mut disk = Disk::new();
        disk.half(9 * BLOCK_SIZE + 28, 12);
        disk.entry(9 * BLOCK_SIZE + 36, 12, 264, &[b'x'; 255]);
        disk.entry(9 * BLOCK_SIZE + 300, 12, 264, &[b'y'; 255]);
        disk.entry(9 * BLOCK_SIZE + 564, 12, 460, &[b'z'; 255]);
        disk
    }

    #[test]
    fn creates_zeroed_files_with_metadata_and_persistent_allocation_counts() {
        let mut disk = Disk::new();
        disk.bytes[Disk::inode_offset(13)..Disk::inode_offset(14)].fill(255);
        let mut volume = Volume::mount(disk).unwrap();
        let inode = volume.create_file(ROOT, b"new", 0o640, 17, 19).unwrap();
        assert_eq!(inode, Inode::new(InodeNumber(13), REGULAR | 0o640, 17, 19));
        assert_eq!(volume.lookup(b"/new"), Ok(inode.clone()));
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(19));
        assert_eq!(read_u16(&volume.device.bytes, 2062), Ok(19));
        assert_eq!(read_u16(&volume.device.bytes, 2064), Ok(1));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(53));
        assert_eq!(volume.device.bytes[4 * BLOCK_SIZE + 1], 31);
        assert_eq!(volume.write(inode.number, 1027, b"new data"), Ok(8));
        let mut remounted = Volume::mount(volume.device).unwrap();
        assert_eq!(remounted.lookup(b"/new").unwrap().size, 1035);
        let mut bytes = [255; 1035];
        assert_eq!(remounted.read(inode.number, 0, &mut bytes), Ok(1035));
        assert!(bytes[..1027].iter().all(|byte| *byte == 0));
        assert_eq!(&bytes[1027..], b"new data");
    }

    #[test]
    fn creation_clears_large_inode_records_without_allocating_file_data() {
        let mut disk = Disk::new();
        let root: [u8; 128] = disk.bytes[Disk::inode_offset(2)..Disk::inode_offset(3)]
            .try_into()
            .unwrap();
        let file: [u8; 128] = disk.bytes[Disk::inode_offset(12)..Disk::inode_offset(13)]
            .try_into()
            .unwrap();
        disk.bytes
            .copy_within(9 * BLOCK_SIZE..11 * BLOCK_SIZE, 13 * BLOCK_SIZE);
        disk.bytes[5 * BLOCK_SIZE..13 * BLOCK_SIZE].fill(0);
        let root_offset = 5 * BLOCK_SIZE + 256;
        let file_offset = 5 * BLOCK_SIZE + 11 * 256;
        let new_offset = 5 * BLOCK_SIZE + 12 * 256;
        disk.bytes[root_offset..root_offset + 128].copy_from_slice(&root);
        disk.bytes[file_offset..file_offset + 128].copy_from_slice(&file);
        disk.bytes[new_offset..new_offset + 256].fill(255);
        disk.half(1112, 256);
        disk.word(root_offset + 40, 13);
        disk.word(file_offset + 40, 14);
        disk.word(1036, 0);
        disk.half(2060, 0);
        disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
        let mut volume = Volume::mount(disk).unwrap();
        let created = volume
            .create_file(ROOT, b"large-inode", 0o644, 5, 6)
            .unwrap();
        assert_eq!(created.number, InodeNumber(13));
        assert_eq!(volume.lookup(b"/large-inode"), Ok(created));
        assert!(
            volume.device.bytes[new_offset + 128..new_offset + 256]
                .iter()
                .all(|byte| *byte == 0)
        );
        assert_eq!(
            &volume.device.bytes[file_offset..file_offset + 40],
            &file[..40]
        );
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(0));
        assert_eq!(volume.unlink(ROOT, b"large-inode"), Ok(()));
        assert!(
            volume.device.bytes[new_offset..new_offset + 256]
                .iter()
                .all(|byte| *byte == 0)
        );
    }

    #[test]
    fn mkdir_initializes_dot_entries_and_updates_parent_and_group_counts() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let directory = volume.mkdir(ROOT, b"dir", 0o750, 5, 6).unwrap();
        assert!(directory.is_directory());
        assert_eq!(directory.links, 2);
        assert_eq!(directory.sectors, 2);
        assert_eq!(directory.size, BLOCK_SIZE as u64);
        assert_eq!(volume.lookup(b"/dir/."), Ok(directory.clone()));
        assert_eq!(volume.lookup(b"/dir/..").unwrap().number, ROOT);
        assert_eq!(volume.inode(ROOT).unwrap().links, 3);
        assert_eq!(read_u16(&volume.device.bytes, 2064), Ok(2));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(52));
        let file = volume
            .create_file(directory.number, b"child", 0o600, 5, 6)
            .unwrap();
        assert_eq!(volume.lookup(b"/dir/child"), Ok(file));
        let child = volume
            .mkdir(directory.number, b"nested", 0o700, 5, 6)
            .unwrap();
        let mut remounted = Volume::mount(volume.device).unwrap();
        assert_eq!(remounted.lookup(b"/dir/nested"), Ok(child));
        assert_eq!(
            remounted.lookup(b"/dir/nested/..").unwrap().number,
            directory.number
        );
        assert_eq!(remounted.inode(directory.number).unwrap().links, 3);
    }

    #[test]
    fn rejects_invalid_names_duplicates_modes_and_parent_types_without_writes() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let before = volume.device.bytes.clone();
        for name in [b"".as_slice(), b".", b"..", b"a/b", b"a\0b", &[b'x'; 256]] {
            assert_eq!(
                volume.create_file(ROOT, name, 0o644, 0, 0),
                Err(Error::InvalidPath)
            );
            assert_eq!(
                volume.mkdir(ROOT, name, 0o755, 0, 0),
                Err(Error::InvalidPath)
            );
        }
        assert_eq!(
            volume.create_file(ROOT, b"file", 0o644, 0, 0),
            Err(Error::AlreadyExists)
        );
        assert_eq!(
            volume.mkdir(ROOT, b"file", 0o755, 0, 0),
            Err(Error::AlreadyExists)
        );
        assert_eq!(
            volume.create_file(ROOT, b"new", REGULAR, 0, 0),
            Err(Error::Unsupported)
        );
        assert_eq!(
            volume.mkdir(ROOT, b"new", DIRECTORY, 0, 0),
            Err(Error::Unsupported)
        );
        assert_eq!(
            volume.mkdir(FILE, b"new", 0o755, 0, 0),
            Err(Error::NotDirectory)
        );
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn reuses_deleted_directory_records_and_supports_legacy_name_lengths() {
        for revision in [0, 1] {
            let mut disk = Disk::new();
            disk.word(1100, revision);
            disk.word(1120, 0);
            for offset in [0, 12, 24] {
                disk.bytes[9 * BLOCK_SIZE + offset + 7] = 0;
            }
            disk.word(9 * BLOCK_SIZE + 24, 0);
            let mut volume = Volume::mount(disk).unwrap();
            let directory = volume.mkdir(ROOT, &[b'x'; 255], 0o755, 0, 0).unwrap();
            assert_eq!(volume.inode(ROOT).unwrap().size, BLOCK_SIZE as u64);
            assert_eq!(volume.entries(ROOT).unwrap()[2].inode, directory.number);
            assert_eq!(volume.entries(directory.number).unwrap().len(), 2);
            let bytes = volume.block(directory.blocks[0]).unwrap();
            assert_eq!(read_u16(&bytes, 6), Ok(1));
            assert_eq!(read_u16(&bytes, 18), Ok(2));
        }
    }

    #[test]
    fn directory_growth_allocates_single_indirect_blocks_and_remounts() {
        let mut disk = Disk::new();
        disk.bytes.resize(256 * BLOCK_SIZE, 0);
        disk.bytes
            .copy_within(9 * BLOCK_SIZE..11 * BLOCK_SIZE, 69 * BLOCK_SIZE);
        disk.bytes[9 * BLOCK_SIZE..69 * BLOCK_SIZE].fill(0);
        disk.word(1024, 512);
        disk.word(1028, 256);
        disk.word(1064, 512);
        disk.word(1040, 500);
        disk.half(2062, 500);
        disk.word(1036, 185);
        disk.half(2060, 185);
        disk.word(Disk::inode_offset(2) + 40, 69);
        disk.word(Disk::inode_offset(12) + 40, 70);
        disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
        for bit in 70..255 {
            disk.bytes[3 * BLOCK_SIZE + bit / 8] &= !(1 << (bit % 8));
        }
        let mut volume = Volume::mount(disk).unwrap();
        for index in 0..50 {
            let name = alloc::format!("{index:0255}");
            volume
                .create_file(ROOT, name.as_bytes(), 0o644, 0, 0)
                .unwrap();
        }
        let directory = volume.inode(ROOT).unwrap();
        assert!(directory.size > 12 * BLOCK_SIZE as u64);
        assert_ne!(directory.blocks[12], 0);
        assert_eq!(directory.sectors, (directory.size / 512) as u32 + 2);
        let mut remounted = Volume::mount(volume.device).unwrap();
        assert_eq!(remounted.entries(ROOT).unwrap().len(), 53);
        for index in 0..50 {
            let path = alloc::format!("/{index:0255}");
            assert!(remounted.lookup(path.as_bytes()).unwrap().is_file());
        }
        for index in 0..50 {
            let name = alloc::format!("{index:0255}");
            assert_eq!(remounted.unlink(ROOT, name.as_bytes()), Ok(()));
        }
        assert_eq!(remounted.entries(ROOT).unwrap().len(), 3);
        assert_eq!(remounted.inode(ROOT).unwrap().size, directory.size);
    }

    #[test]
    fn inode_allocation_moves_to_the_second_group() {
        for (inodes, bitmap, free, allocated) in [(64, 0, 32, 33), (36, 7, 1, 36)] {
            let mut disk = Disk::new();
            disk.bytes.resize(9000 * BLOCK_SIZE, 0);
            disk.word(1028, 9000);
            disk.word(1024, inodes);
            disk.bytes[4 * BLOCK_SIZE..5 * BLOCK_SIZE].fill(255);
            disk.half(2062, 0);
            disk.word(1040, free);
            disk.word(2080, 8195);
            disk.word(2084, 8196);
            disk.word(2088, 8197);
            disk.half(2092, 799);
            disk.half(2094, free as u16);
            disk.bytes[8196 * BLOCK_SIZE] = bitmap;
            disk.word(1036, 852);
            disk.bytes[8195 * BLOCK_SIZE..8196 * BLOCK_SIZE].fill(255);
            for bit in 8..807 {
                disk.bytes[8195 * BLOCK_SIZE + bit / 8] &= !(1 << (bit % 8));
            }
            let mut volume = Volume::mount(disk).unwrap();
            let directory = volume.mkdir(ROOT, b"group2", 0o755, 9, 10).unwrap();
            assert_eq!(directory.number, InodeNumber(allocated));
            assert_eq!(volume.lookup(b"/group2"), Ok(directory.clone()));
            assert_eq!(read_u16(&volume.device.bytes, 2062), Ok(0));
            assert_eq!(read_u16(&volume.device.bytes, 2094), Ok(free as u16 - 1));
            assert_eq!(read_u16(&volume.device.bytes, 2096), Ok(1));
            assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(free - 1));
            assert_eq!(
                volume.device.bytes[8196 * BLOCK_SIZE],
                if allocated == 33 { 1 } else { 15 }
            );
            if free == 1 {
                let before = volume.device.bytes.clone();
                assert_eq!(
                    volume.create_file(ROOT, b"full", 0o644, 0, 0),
                    Err(Error::NoSpace)
                );
                assert_eq!(volume.device.bytes, before);
            }
            assert_eq!(volume.link(directory.number, b"alias", FILE), Ok(()));
            assert_eq!(volume.lookup(b"/group2/alias").unwrap().number, FILE);
            assert_eq!(volume.inode(FILE).unwrap().links, 2);
            assert_eq!(volume.unlink(directory.number, b"alias"), Ok(()));
            assert_eq!(volume.inode(FILE).unwrap().links, 1);
            assert_eq!(volume.rmdir(ROOT, b"group2"), Ok(()));
            assert_eq!(read_u16(&volume.device.bytes, 2062), Ok(0));
            assert_eq!(read_u16(&volume.device.bytes, 2094), Ok(free as u16));
            assert_eq!(read_u16(&volume.device.bytes, 2096), Ok(0));
            assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(free));
            assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(852));
        }
    }

    #[test]
    fn namespace_space_exhaustion_and_corrupt_metadata_leave_disk_unchanged() {
        for (offset, value, expected) in [
            (1040, 0, Error::NoSpace),
            (1036, 0, Error::NoSpace),
            (1040, 33, Error::Corrupt),
            (1108, 2, Error::Corrupt),
            (1108, 34, Error::Corrupt),
            (1052, 1, Error::Unsupported),
            (1116, 0x80, Error::Unsupported),
            (2048, 5, Error::Corrupt),
            (Disk::inode_offset(2) + 32, 0x1000, Error::Unsupported),
            (Disk::inode_offset(2) + 4, 1023, Error::Corrupt),
        ] {
            let mut disk = Disk::new();
            disk.word(offset, value);
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                volume.mkdir(ROOT, b"new", 0o755, 0, 0),
                Err(expected),
                "offset {offset}"
            );
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(2) + 26, u16::MAX);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.mkdir(ROOT, b"new", 0o755, 0, 0),
            Err(Error::TooManyLinks)
        );
        assert_eq!(volume.device.writes, 0);
        assert!(volume.create_file(ROOT, b"file2", 0o644, 0, 0).is_ok());
    }

    #[test]
    fn namespace_staging_failures_and_partial_commit_failures_restore_every_byte() {
        for (directory, growth) in [(false, false), (true, false), (false, true), (true, true)] {
            let disk = || {
                if growth {
                    full_directory_disk()
                } else {
                    Disk::new()
                }
            };
            let name = if growth { &[b'n'; 255][..] } else { b"new" };
            let create = |volume: &mut Volume<Disk>| {
                if directory {
                    volume.mkdir(ROOT, name, 0o755, 4, 5)
                } else {
                    volume.create_file(ROOT, name, 0o644, 4, 5)
                }
            };
            let mut control = Volume::mount(disk()).unwrap();
            let first = control.device.reads + 1;
            create(&mut control).unwrap();
            for fail_at in first..=control.device.reads {
                let mut volume = Volume::mount(disk()).unwrap();
                let before = volume.device.bytes.clone();
                volume.device.fail_reads.push(fail_at);
                assert!(matches!(create(&mut volume), Err(Error::Device(_))));
                assert_eq!(volume.device.bytes, before, "staging read {fail_at}");
                assert_eq!(volume.device.writes, 0);
            }
            for fail_at in 1..=control.device.writes {
                let mut volume = Volume::mount(disk()).unwrap();
                let before = volume.device.bytes.clone();
                volume.device.fail_writes.push(fail_at);
                assert!(matches!(create(&mut volume), Err(Error::Device(_))));
                assert_eq!(volume.device.bytes, before, "commit write {fail_at}");
                assert!(
                    !volume
                        .entries(ROOT)
                        .unwrap()
                        .iter()
                        .any(|entry| entry.name == name)
                );
                assert!(create(&mut volume).is_ok());
            }
        }
    }

    #[test]
    fn failed_namespace_rollback_requires_recovery() {
        let mut disk = Disk::new();
        disk.fail_writes.extend([2, 3]);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.mkdir(ROOT, b"new", 0o755, 0, 0),
            Err(Error::RecoveryRequired)
        );
        let writes = volume.device.writes;
        assert_eq!(
            volume.create_file(ROOT, b"new", 0o644, 0, 0),
            Err(Error::RecoveryRequired)
        );
        assert_eq!(volume.entries(ROOT), Err(Error::RecoveryRequired));
        assert_eq!(volume.device.writes, writes);
    }

    #[test]
    fn inode_bitmap_and_descriptor_corruption_refuse_namespace_writes() {
        for case in 0..6 {
            let mut disk = Disk::new();
            match case {
                0 => disk.bytes[4 * BLOCK_SIZE] &= !2,
                1 => disk.bytes[4 * BLOCK_SIZE] &= !1,
                2 => disk.bytes[4 * BLOCK_SIZE..5 * BLOCK_SIZE].fill(255),
                3 => disk.half(2062, 33),
                4 => disk.half(2064, 13),
                5 => disk.word(9 * BLOCK_SIZE + 24 + 4, 0),
                _ => unreachable!(),
            }
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                volume.mkdir(ROOT, b"new", 0o755, 0, 0),
                Err(Error::Corrupt),
                "case {case}"
            );
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
    }

    #[test]
    fn directory_growth_failure_does_not_leak_inode_or_child_directory_block() {
        let mut disk = full_directory_disk();
        disk.word(1036, 1);
        disk.half(2060, 1);
        disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
        disk.bytes[3 * BLOCK_SIZE + 1] &= !4;
        let mut volume = Volume::mount(disk).unwrap();
        let before = volume.device.bytes.clone();
        assert_eq!(
            volume.mkdir(ROOT, &[b'n'; 255], 0o755, 0, 0),
            Err(Error::NoSpace)
        );
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn overwrite_preserves_length_neighbors_and_allocation_counts() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let before = volume.device.bytes.clone();
        assert_eq!(volume.write(FILE, 1, b"ABC"), Ok(3));
        let mut bytes = [0; 5];
        assert_eq!(volume.read(FILE, 0, &mut bytes), Ok(5));
        assert_eq!(&bytes, b"hABCo");
        assert_eq!(volume.inode(FILE).unwrap().size, 5);
        assert_eq!(volume.inode(FILE).unwrap().sectors, 2);
        assert_eq!(
            volume.device.bytes[..5 * BLOCK_SIZE],
            before[..5 * BLOCK_SIZE]
        );
    }

    #[test]
    fn growth_zeroes_stale_tail_and_holes_and_accounts_for_new_blocks() {
        let mut disk = Disk::new();
        disk.bytes[10 * BLOCK_SIZE + 5..11 * BLOCK_SIZE].fill(91);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.write(FILE, 3 * BLOCK_SIZE as u64 + 7, b"end"), Ok(3));
        let inode = volume.inode(FILE).unwrap();
        assert_eq!(inode.size, 3 * BLOCK_SIZE as u64 + 10);
        assert_eq!(inode.sectors, 4);
        let mut observed = alloc::vec![255; inode.size as usize];
        volume.read(FILE, 0, &mut observed).unwrap();
        assert_eq!(&observed[..5], b"hello");
        assert!(
            observed[5..observed.len() - 3]
                .iter()
                .all(|byte| *byte == 0)
        );
        assert_eq!(&observed[observed.len() - 3..], b"end");
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(52));
        assert_eq!(read_u16(&volume.device.bytes, 2060), Ok(52));
        assert_eq!(volume.device.bytes[3 * BLOCK_SIZE + 1] & 4, 4);
    }

    #[test]
    fn extending_over_preallocated_blocks_does_not_expose_stale_bytes() {
        let mut disk = Disk::new();
        disk.word(Disk::inode_offset(12) + 44, 11);
        disk.word(Disk::inode_offset(12) + 28, 4);
        disk.bytes[3 * BLOCK_SIZE + 1] |= 4;
        disk.bytes[11 * BLOCK_SIZE..12 * BLOCK_SIZE].fill(91);
        disk.word(1036, 52);
        disk.half(2060, 52);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.write(FILE, 1500, b"end"), Ok(3));
        let mut observed = alloc::vec![255; 1503];
        assert_eq!(volume.read(FILE, 0, &mut observed), Ok(1503));
        assert_eq!(&observed[..5], b"hello");
        assert!(observed[5..1500].iter().all(|byte| *byte == 0));
        assert_eq!(&observed[1500..], b"end");
        assert_eq!(volume.inode(FILE).unwrap().sectors, 4);
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(52));
    }

    #[test]
    fn unsupported_features_and_overlapping_group_metadata_refuse_writes() {
        for (offset, value, expected) in [
            (1052, 1, Error::Unsupported),
            (1116, 0x80, Error::Unsupported),
            (2048, 5, Error::Corrupt),
            (2048, 0, Error::Corrupt),
            (2052, 3, Error::Corrupt),
        ] {
            let mut disk = Disk::new();
            disk.word(offset, value);
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(volume.write(FILE, 0, b"x"), Err(expected));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
    }

    #[test]
    fn grows_and_overwrites_across_direct_single_and_double_indirection() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        for logical in [11, 12, 267, 268, 524] {
            let offset = logical * BLOCK_SIZE as u64 + 1022;
            assert_eq!(volume.write(FILE, offset, b"ABCD"), Ok(4));
            let mut observed = [0; 4];
            assert_eq!(volume.read(FILE, offset, &mut observed), Ok(4));
            assert_eq!(&observed, b"ABCD");
        }
        let inode = volume.inode(FILE).unwrap();
        assert_ne!(inode.blocks[12], 0);
        assert_ne!(inode.blocks[13], 0);
        let before = inode.sectors;
        assert_eq!(
            volume.write(FILE, 268 * BLOCK_SIZE as u64 + 1022, b"last"),
            Ok(4)
        );
        assert_eq!(volume.inode(FILE).unwrap().sectors, before);
        assert_eq!(
            read_u32(&volume.device.bytes, 1036).unwrap(),
            53 - (before - 2) / 2
        );
    }

    #[test]
    fn allocation_uses_the_second_group_and_keeps_the_first_group_count_unchanged() {
        let mut disk = Disk::new();
        disk.bytes.resize(9000 * BLOCK_SIZE, 0);
        disk.word(1028, 9000);
        disk.word(1024, 64);
        disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
        disk.half(2060, 0);
        disk.word(2048 + 32, 8195);
        disk.word(2048 + 32 + 4, 8196);
        disk.word(2048 + 32 + 8, 8197);
        disk.half(2048 + 32 + 12, 799);
        disk.word(1036, 799);
        disk.bytes[8195 * BLOCK_SIZE..8196 * BLOCK_SIZE].fill(255);
        for bit in 8..807 {
            disk.bytes[8195 * BLOCK_SIZE + bit / 8] &= !(1 << (bit % 8));
        }
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.write(FILE, 1024, b"next"), Ok(4));
        let inode = volume.inode(FILE).unwrap();
        assert_eq!(inode.blocks[1], 8201);
        assert_eq!(inode.sectors, 4);
        assert_eq!(read_u16(&volume.device.bytes, 2060), Ok(0));
        assert_eq!(read_u16(&volume.device.bytes, 2048 + 32 + 12), Ok(798));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(798));
        assert_eq!(
            &volume.device.bytes[8201 * BLOCK_SIZE..8201 * BLOCK_SIZE + 4],
            b"next"
        );
        assert_eq!(volume.unlink(ROOT, b"file"), Ok(()));
        assert_eq!(read_u16(&volume.device.bytes, 2060), Ok(1));
        assert_eq!(read_u16(&volume.device.bytes, 2048 + 32 + 12), Ok(799));
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(800));
    }

    #[test]
    fn can_allocate_the_last_supported_double_indirect_slot() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let last = u64::from(MAX_FILE_BLOCKS) * BLOCK_SIZE as u64 - 1;
        assert_eq!(volume.write(FILE, last, b"z"), Ok(1));
        assert_eq!(volume.inode(FILE).unwrap().size, last + 1);
        assert_eq!(volume.inode(FILE).unwrap().sectors, 8);
        let mut observed = [0];
        assert_eq!(volume.read(FILE, last, &mut observed), Ok(1));
        assert_eq!(&observed, b"z");
    }

    #[test]
    fn invalid_indirect_pointers_leave_all_disk_bytes_unchanged() {
        let mut disk = Disk::new();
        disk.word(Disk::inode_offset(12) + 40 + 12 * 4, 64);
        let before = disk.bytes.clone();
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.write(FILE, 12 * BLOCK_SIZE as u64, b"x"),
            Err(Error::Corrupt)
        );
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn staging_read_failures_leave_all_disk_bytes_unchanged() {
        let mut control = Volume::mount(Disk::new()).unwrap();
        let first = control.device.reads + 1;
        control
            .write(FILE, 268 * BLOCK_SIZE as u64, b"payload")
            .unwrap();
        for fail_at in first..=control.device.reads {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let before = volume.device.bytes.clone();
            volume.device.fail_reads.push(fail_at);
            assert_eq!(
                volume.write(FILE, 268 * BLOCK_SIZE as u64, b"payload"),
                Err(Error::Device(block::Error::Native(hunde::abi::IoError(
                    -71
                ))))
            );
            assert_eq!(volume.device.bytes, before, "failed staging read {fail_at}");
            assert_eq!(volume.device.writes, 0);
        }
    }

    #[test]
    fn out_of_space_during_indirect_allocation_leaves_disk_unchanged() {
        let mut disk = Disk::new();
        disk.bytes[3 * BLOCK_SIZE..4 * BLOCK_SIZE].fill(255);
        disk.bytes[3 * BLOCK_SIZE + 1] &= !12;
        disk.word(1036, 2);
        disk.half(2060, 2);
        let before = disk.bytes.clone();
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(
            volume.write(FILE, 268 * BLOCK_SIZE as u64, b"x"),
            Err(Error::NoSpace)
        );
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn invalid_requests_and_immutable_flags_do_not_write() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        assert_eq!(volume.write(FILE, 0, b""), Ok(0));
        assert_eq!(volume.write(FILE, u64::MAX, b"x"), Err(Error::FileTooLarge));
        assert_eq!(
            volume.write(FILE, u64::from(MAX_FILE_BLOCKS) * BLOCK_SIZE as u64, b"x"),
            Err(Error::FileTooLarge)
        );
        assert_eq!(volume.write(InodeNumber(2), 0, b"x"), Err(Error::NotFile));
        volume.device.word(Disk::inode_offset(12) + 32, 0x10);
        assert_eq!(volume.write(FILE, 0, b"x"), Err(Error::Unsupported));
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn refuses_free_metadata_and_data_pointers_to_metadata_or_free_blocks() {
        for number in [1, 3, 5, 11] {
            let mut disk = Disk::new();
            disk.word(Disk::inode_offset(12) + 40, number);
            let before = disk.bytes.clone();
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(volume.write(FILE, 0, b"x"), Err(Error::Corrupt));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.device.writes, 0);
        }
        let mut disk = Disk::new();
        disk.bytes[3 * BLOCK_SIZE] &= !1;
        let before = disk.bytes.clone();
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.write(FILE, 1024, b"x"), Err(Error::Corrupt));
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.device.writes, 0);
    }

    #[test]
    fn rolls_back_every_commit_failure_including_partial_writes() {
        let mut control = Volume::mount(Disk::new()).unwrap();
        control
            .write(FILE, 268 * BLOCK_SIZE as u64, b"payload")
            .unwrap();
        for fail_at in 1..=control.device.writes {
            let mut disk = Disk::new();
            let before = disk.bytes.clone();
            disk.fail_writes.push(fail_at);
            let mut volume = Volume::mount(disk).unwrap();
            assert_eq!(
                volume.write(FILE, 268 * BLOCK_SIZE as u64, b"payload"),
                Err(Error::Device(block::Error::Native(hunde::abi::IoError(
                    -72
                ))))
            );
            assert_eq!(volume.device.bytes, before, "failed commit write {fail_at}");
            assert_eq!(volume.inode(FILE).unwrap().size, 5);
            assert_eq!(volume.write(FILE, 0, b"retry"), Ok(5));
        }
    }

    #[test]
    fn failed_rollback_disables_further_volume_io() {
        let mut disk = Disk::new();
        disk.fail_writes.extend([2, 3]);
        let mut volume = Volume::mount(disk).unwrap();
        assert_eq!(volume.write(FILE, 1024, b"x"), Err(Error::RecoveryRequired));
        let writes = volume.device.writes;
        assert_eq!(volume.read(FILE, 0, &mut [0]), Err(Error::RecoveryRequired));
        assert_eq!(volume.write(FILE, 0, b"x"), Err(Error::RecoveryRequired));
        assert_eq!(volume.device.writes, writes);
    }
}

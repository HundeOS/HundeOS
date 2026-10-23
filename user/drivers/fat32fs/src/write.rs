use super::*;

struct Change {
    sector: Sector,
    original: [u8; SECTOR_SIZE],
    updated: [u8; SECTOR_SIZE],
}

struct Mutation<'a, D> {
    volume: &'a mut Volume<D>,
    changes: Vec<Change>,
    fat_cache: Option<(Sector, [u8; SECTOR_SIZE])>,
    next_free: u32,
    free_change: i64,
}

fn store_word(bytes: &mut [u8], offset: usize, value: u16) {
    bytes[offset..offset + 2].copy_from_slice(&value.to_le_bytes());
}

fn store_double_word(bytes: &mut [u8], offset: usize, value: u32) {
    bytes[offset..offset + 4].copy_from_slice(&value.to_le_bytes());
}

fn store_cluster(bytes: &mut [u8], cluster: Cluster) {
    store_word(bytes, 20, (cluster.0 >> 16) as u16);
    store_word(bytes, 26, cluster.0 as u16);
}

fn parent_name(path: &str) -> Result<(&str, &str), Error> {
    validate_path(path)?;
    if path == "/" {
        return Err(Error::InvalidPath);
    }
    let (parent, name) = path.rsplit_once('/').ok_or(Error::InvalidPath)?;
    Ok((if parent.is_empty() { "/" } else { parent }, name))
}

fn short_alias(name: &str) -> Result<[u8; 11], Error> {
    let (base, extension) = name.split_once('.').unwrap_or((name, ""));
    if base.is_empty()
        || base.len() > 8
        || extension.len() > 3
        || extension.contains('.')
        || (name.contains('.') && extension.is_empty())
        || name.contains(' ')
    {
        return Err(Error::InvalidPath);
    }
    short_field(base.as_bytes()).map_err(|_| Error::InvalidPath)?;
    short_field(extension.as_bytes()).map_err(|_| Error::InvalidPath)?;
    let mut alias = [b' '; 11];
    alias[..base.len()].copy_from_slice(base.as_bytes());
    alias[8..8 + extension.len()].copy_from_slice(extension.as_bytes());
    alias.make_ascii_uppercase();
    Ok(alias)
}

impl<D: Device> Volume<D> {
    fn chain(&mut self, start: Cluster) -> Result<Vec<Cluster>, Error> {
        let mut clusters = Vec::new();
        if start.0 == 0 {
            return Ok(clusters);
        }
        let mut chain = Chain::new(start, self.geometry.clusters);
        while let Some(cluster) = chain.next(self)? {
            clusters.try_reserve(1).map_err(|_| Error::OutOfMemory)?;
            clusters.push(cluster);
        }
        Ok(clusters)
    }

    fn mutable_entry(&mut self, path: &str, kind: Kind) -> Result<Entry, Error> {
        let entry = self.entry(path)?;
        if entry.kind != kind {
            return Err(if kind == Kind::File {
                Error::NotFile
            } else {
                Error::NotDirectory
            });
        }
        if entry.attributes & 1 != 0 {
            return Err(Error::AccessDenied);
        }
        Ok(entry)
    }

    fn file_chain(&mut self, path: &str, file: &Entry) -> Result<Vec<Cluster>, Error> {
        let chain = self.chain(file.cluster)?;
        let capacity =
            chain.len() as u64 * u64::from(self.geometry.sectors_per_cluster) * SECTOR_SIZE as u64;
        if capacity < u64::from(file.size) {
            return Err(Error::Corrupt);
        }
        let (parent, _) = path.rsplit_once('/').ok_or(Error::InvalidPath)?;
        let parent = self.entry(if parent.is_empty() { "/" } else { parent })?;
        let parent_chain = self.chain(parent.cluster)?;
        let root = self.chain(self.geometry.root)?;
        if chain
            .iter()
            .any(|cluster| parent_chain.contains(cluster) || root.contains(cluster))
        {
            return Err(Error::Corrupt);
        }
        Ok(chain)
    }

    pub fn create_file(&mut self, path: &str) -> Result<(), Error> {
        self.create(path, Kind::File)
    }

    pub fn create_directory(&mut self, path: &str) -> Result<(), Error> {
        self.create(path, Kind::Directory)
    }

    fn create(&mut self, path: &str, kind: Kind) -> Result<(), Error> {
        if self.recovery_required {
            return Err(Error::RecoveryRequired);
        }
        let (parent_path, name) = parent_name(path)?;
        let name = short_alias(name)?;
        match self.entry(path) {
            Ok(_) => return Err(Error::AlreadyExists),
            Err(Error::NotFound) => {}
            Err(error) => return Err(error),
        }
        let parent = self.mutable_entry(parent_path, Kind::Directory)?;
        let parent_chain = self.chain(parent.cluster)?;
        let mut mutation = Mutation::new(self);
        let slot = mutation.free_slot(&parent_chain)?;
        let mut record = [0; 32];
        record[..11].copy_from_slice(&name);
        record[11] = if kind == Kind::Directory { 0x10 } else { 0x20 };
        if kind == Kind::Directory {
            let cluster = mutation.allocate()?;
            store_cluster(&mut record, cluster);
            let sector = mutation.volume.geometry.sector(cluster)?;
            let parent_cluster = if parent.cluster == mutation.volume.geometry.root {
                Cluster(0)
            } else {
                parent.cluster
            };
            let bytes = mutation.edit(Sector(sector))?;
            bytes[..11].copy_from_slice(b".          ");
            bytes[11] = 0x10;
            store_cluster(&mut bytes[..32], cluster);
            bytes[32..43].copy_from_slice(b"..         ");
            bytes[43] = 0x10;
            store_cluster(&mut bytes[32..64], parent_cluster);
        }
        mutation.edit(slot.sector)?[slot.offset..slot.offset + 32].copy_from_slice(&record);
        mutation.commit()
    }

    pub fn write(&mut self, path: &str, offset: u64, source: &[u8]) -> Result<usize, Error> {
        let file = self.mutable_entry(path, Kind::File)?;
        if source.is_empty() {
            return Ok(0);
        }
        let end = offset
            .checked_add(source.len() as u64)
            .filter(|end| *end <= u64::from(u32::MAX))
            .ok_or(Error::FileTooLarge)?;
        let mut chain = self.file_chain(path, &file)?;
        let cluster_bytes = u64::from(self.geometry.sectors_per_cluster) * SECTOR_SIZE as u64;
        if (chain.len() as u64) * cluster_bytes < u64::from(file.size) {
            return Err(Error::Corrupt);
        }
        let required = end.div_ceil(cluster_bytes);
        if required > u64::from(self.geometry.clusters) {
            return Err(Error::NoSpace);
        }
        chain
            .try_reserve((required as usize).saturating_sub(chain.len()))
            .map_err(|_| Error::OutOfMemory)?;
        let mut mutation = Mutation::new(self);
        while (chain.len() as u64) < required {
            let cluster = mutation.allocate()?;
            if let Some(previous) = chain.last() {
                mutation.set_fat(*previous, cluster.0)?
            }
            chain.push(cluster);
        }
        if offset > u64::from(file.size) {
            mutation.fill(
                &chain,
                u64::from(file.size),
                offset - u64::from(file.size),
                None,
            )?;
        }
        mutation.fill(&chain, offset, source.len() as u64, Some(source))?;
        let slot = file.location.ok_or(Error::Corrupt)?;
        let record = &mut mutation.edit(slot.sector)?[slot.offset..slot.offset + 32];
        store_cluster(record, *chain.first().ok_or(Error::Corrupt)?);
        store_double_word(record, 28, file.size.max(end as u32));
        record[11] |= 0x20;
        mutation.commit()?;
        Ok(source.len())
    }

    pub fn truncate(&mut self, path: &str) -> Result<(), Error> {
        let file = self.mutable_entry(path, Kind::File)?;
        let chain = self.file_chain(path, &file)?;
        let mut mutation = Mutation::new(self);
        for cluster in chain {
            mutation.set_fat(cluster, 0)?
        }
        let slot = file.location.ok_or(Error::Corrupt)?;
        let record = &mut mutation.edit(slot.sector)?[slot.offset..slot.offset + 32];
        store_cluster(record, Cluster(0));
        store_double_word(record, 28, 0);
        record[11] |= 0x20;
        mutation.commit()
    }

    pub fn remove_file(&mut self, path: &str) -> Result<(), Error> {
        self.remove(path, Kind::File)
    }

    pub fn remove_directory(&mut self, path: &str) -> Result<(), Error> {
        self.remove(path, Kind::Directory)
    }

    fn remove(&mut self, path: &str, kind: Kind) -> Result<(), Error> {
        let entry = self.mutable_entry(path, kind)?;
        let slot = entry.location.ok_or(Error::AccessDenied)?;
        if kind == Kind::Directory && !self.list(path, 0, 1)?.is_empty() {
            return Err(Error::NotEmpty);
        }
        let (parent_path, _) = parent_name(path)?;
        let parent = self.mutable_entry(parent_path, Kind::Directory)?;
        let parent_chain = self.chain(parent.cluster)?;
        let chain = if kind == Kind::File {
            self.file_chain(path, &entry)?
        } else {
            self.chain(entry.cluster)?
        };
        if chain.iter().any(|cluster| parent_chain.contains(cluster)) {
            return Err(Error::Corrupt);
        }
        let mut mutation = Mutation::new(self);
        let slots = mutation.long_slots(&parent_chain, slot)?;
        for cluster in chain {
            mutation.set_fat(cluster, 0)?
        }
        for location in slots {
            mutation.edit(location.sector)?[location.offset] = 0xe5
        }
        mutation.edit(slot.sector)?[slot.offset] = 0xe5;
        mutation.commit()
    }
}

impl<'a, D: Device> Mutation<'a, D> {
    fn new(volume: &'a mut Volume<D>) -> Self {
        Self {
            volume,
            changes: Vec::new(),
            fat_cache: None,
            next_free: 2,
            free_change: 0,
        }
    }

    fn read(&mut self, sector: Sector) -> Result<[u8; SECTOR_SIZE], Error> {
        if let Some(change) = self.changes.iter().find(|change| change.sector == sector) {
            return Ok(change.updated);
        }
        let mut bytes = [0; SECTOR_SIZE];
        self.volume.device.read(sector, &mut bytes)?;
        Ok(bytes)
    }

    fn edit(&mut self, sector: Sector) -> Result<&mut [u8; SECTOR_SIZE], Error> {
        let index = match self
            .changes
            .iter()
            .position(|change| change.sector == sector)
        {
            Some(index) => index,
            None => {
                self.changes
                    .try_reserve(1)
                    .map_err(|_| Error::OutOfMemory)?;
                let original = self.read(sector)?;
                self.changes.push(Change {
                    sector,
                    original,
                    updated: original,
                });
                self.changes.len() - 1
            }
        };
        self.fat_cache = None;
        Ok(&mut self.changes[index].updated)
    }

    fn fat(&mut self, cluster: Cluster) -> Result<u32, Error> {
        let offset = u64::from(cluster.0) * 4;
        let sector = Sector(self.volume.geometry.fat_start + offset / SECTOR_SIZE as u64);
        let bytes = match self.fat_cache {
            Some((cached, bytes)) if cached == sector => bytes,
            _ => {
                let bytes = self.read(sector)?;
                self.fat_cache = Some((sector, bytes));
                bytes
            }
        };
        Ok(double_word(&bytes, (offset % SECTOR_SIZE as u64) as usize) & CLUSTER_MASK)
    }

    fn set_fat(&mut self, cluster: Cluster, value: u32) -> Result<(), Error> {
        self.volume.geometry.validate(cluster)?;
        let original = self.fat(cluster)?;
        let offset = u64::from(cluster.0) * 4;
        let geometry = &self.volume.geometry;
        let first = if geometry.mirrored {
            geometry.fat_base
        } else {
            geometry.fat_start
        };
        let count = if geometry.mirrored { geometry.fats } else { 1 };
        let fat_size = geometry.fat_size;
        for index in 0..count {
            let bytes = self.edit(Sector(
                first + index * fat_size + offset / SECTOR_SIZE as u64,
            ))?;
            let index = (offset % SECTOR_SIZE as u64) as usize;
            let old = double_word(bytes, index);
            if old & CLUSTER_MASK != original {
                return Err(Error::Corrupt);
            }
            store_double_word(bytes, index, (old & !CLUSTER_MASK) | value);
        }
        self.free_change += i64::from(value == 0) - i64::from(original == 0);
        Ok(())
    }

    fn allocate(&mut self) -> Result<Cluster, Error> {
        while self.next_free <= self.volume.geometry.clusters + 1 {
            let cluster = Cluster(self.next_free);
            self.next_free += 1;
            if self.fat(cluster)? != 0 {
                continue;
            }
            self.set_fat(cluster, END_OF_CHAIN)?;
            let first = self.volume.geometry.sector(cluster)?;
            for offset in 0..self.volume.geometry.sectors_per_cluster {
                self.edit(Sector(first + u64::from(offset)))?.fill(0);
            }
            return Ok(cluster);
        }
        Err(Error::NoSpace)
    }

    fn free_slot(&mut self, chain: &[Cluster]) -> Result<DirectorySlot, Error> {
        let mut previous_long = false;
        for (cluster_index, cluster) in chain.iter().enumerate() {
            let first = self.volume.geometry.sector(*cluster)?;
            for sector_index in 0..self.volume.geometry.sectors_per_cluster {
                let sector = Sector(first + u64::from(sector_index));
                let bytes = self.read(sector)?;
                for (index, record) in bytes.as_chunks::<32>().0.iter().enumerate() {
                    if record[0] == 0 || (record[0] == 0xe5 && !previous_long) {
                        if record[0] == 0 {
                            if previous_long {
                                return Err(Error::Corrupt);
                            }
                            let next = index + 1;
                            if next < SECTOR_SIZE / 32 {
                                self.edit(sector)?[next * 32] = 0
                            } else if sector_index + 1 < self.volume.geometry.sectors_per_cluster {
                                self.edit(Sector(sector.0 + 1))?[0] = 0
                            } else if let Some(next) = chain.get(cluster_index + 1) {
                                let next = self.volume.geometry.sector(*next)?;
                                self.edit(Sector(next))?[0] = 0;
                            }
                        }
                        return Ok(DirectorySlot::new(sector, index * 32));
                    }
                    previous_long = record[0] != 0xe5 && record[11] == 0xf;
                }
            }
        }
        let previous = *chain.last().ok_or(Error::Corrupt)?;
        if previous_long {
            return Err(Error::Corrupt);
        }
        let cluster = self.allocate()?;
        self.set_fat(previous, cluster.0)?;
        Ok(DirectorySlot::new(
            Sector(self.volume.geometry.sector(cluster)?),
            0,
        ))
    }

    fn long_slots(
        &mut self,
        chain: &[Cluster],
        target: DirectorySlot,
    ) -> Result<Vec<DirectorySlot>, Error> {
        let mut slots = Vec::new();
        let mut ordinal = 0;
        let mut checksum = 0;
        for cluster in chain {
            let first = self.volume.geometry.sector(*cluster)?;
            for offset in 0..self.volume.geometry.sectors_per_cluster {
                let sector = Sector(first + u64::from(offset));
                let bytes = self.read(sector)?;
                for (index, record) in bytes.as_chunks::<32>().0.iter().enumerate() {
                    let location = DirectorySlot::new(sector, index * 32);
                    if location == target {
                        let actual = record[..11]
                            .iter()
                            .fold(0u8, |sum, byte| sum.rotate_right(1).wrapping_add(*byte));
                        if !slots.is_empty() && (ordinal != 0 || checksum != actual) {
                            return Err(Error::Corrupt);
                        }
                        return Ok(slots);
                    }
                    if record[0] == 0 {
                        return Err(Error::Corrupt);
                    }
                    if record[0] == 0xe5 || record[11] != 0xf {
                        slots.clear();
                        ordinal = 0;
                        continue;
                    }
                    if record[0] & 0x40 != 0 {
                        slots.clear();
                        ordinal = record[0] & 0x1f;
                        checksum = record[13]
                    }
                    if record[0] & 0xa0 != 0
                        || ordinal == 0
                        || ordinal > 20
                        || record[0] & 0x1f != ordinal
                        || record[13] != checksum
                        || record[12] != 0
                        || word(record, 26) != 0
                    {
                        return Err(Error::Corrupt);
                    }
                    ordinal -= 1;
                    slots.try_reserve(1).map_err(|_| Error::OutOfMemory)?;
                    slots.push(location);
                }
            }
        }
        Err(Error::Corrupt)
    }

    fn fill(
        &mut self,
        chain: &[Cluster],
        offset: u64,
        length: u64,
        source: Option<&[u8]>,
    ) -> Result<(), Error> {
        let cluster_bytes =
            u64::from(self.volume.geometry.sectors_per_cluster) * SECTOR_SIZE as u64;
        let mut completed = 0;
        while completed < length {
            let position = offset + completed;
            let cluster = *chain
                .get((position / cluster_bytes) as usize)
                .ok_or(Error::Corrupt)?;
            let sector = Sector(
                self.volume.geometry.sector(cluster)?
                    + position % cluster_bytes / SECTOR_SIZE as u64,
            );
            let within = (position % SECTOR_SIZE as u64) as usize;
            let count = ((SECTOR_SIZE - within) as u64).min(length - completed) as usize;
            let destination = &mut self.edit(sector)?[within..within + count];
            match source {
                Some(bytes) => destination
                    .copy_from_slice(&bytes[completed as usize..completed as usize + count]),
                None => destination.fill(0),
            }
            completed += count as u64;
        }
        Ok(())
    }

    fn commit(mut self) -> Result<(), Error> {
        let fat_changed = self.changes.iter().any(|change| {
            (self.volume.geometry.fat_base..self.volume.geometry.data_start)
                .contains(&change.sector.0)
        });
        let info = [
            self.volume.geometry.fsinfo,
            self.volume.geometry.backup_fsinfo,
        ];
        for sector in info.into_iter().flatten().filter(|_| fat_changed) {
            let bytes = self.read(sector)?;
            if double_word(&bytes, 0) == 0x4161_5252
                && double_word(&bytes, 484) == 0x6141_7272
                && double_word(&bytes, 508) == 0xaa55_0000
            {
                let known = double_word(&bytes, 488);
                let free = if known <= self.volume.geometry.clusters {
                    u32::try_from(i64::from(known) + self.free_change)
                        .ok()
                        .filter(|count| *count <= self.volume.geometry.clusters)
                        .unwrap_or(u32::MAX)
                } else {
                    u32::MAX
                };
                let bytes = self.edit(sector)?;
                store_double_word(bytes, 488, free);
                store_double_word(bytes, 492, u32::MAX);
            }
        }
        for (index, change) in self.changes.iter().enumerate() {
            if let Err(error) = self.volume.device.write(change.sector, &change.updated) {
                let mut restored = true;
                for change in self.changes[..=index].iter().rev() {
                    if self
                        .volume
                        .device
                        .write(change.sector, &change.original)
                        .is_err()
                    {
                        restored = false
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

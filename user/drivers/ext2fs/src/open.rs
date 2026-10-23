use super::*;
use hunde::inheritance::Owner;
use hunde::io::{
    FileChange, FileChangeEvents, FileGeneration, FileWatch, Handle, InheritanceToken, ProcessId,
    Reply, SessionId,
};
use hunde::protocol::{
    IO_FILE_CHANGE_DELETE, IO_FILE_CHANGE_TRUNCATE, IO_FILE_CHANGE_WRITE, IO_FILE_WATCH_CAPACITY,
    IO_OPEN_EXECUTE, IO_OPEN_READ, IO_OPEN_WRITE,
};

const HANDLE_LIMIT: usize = 64;
const OWNER_LIMIT: usize = 64;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Access {
    ReadOnly,
    WriteOnly,
    ReadWrite,
    ReadExecute,
    ReadWriteExecute,
}

impl Access {
    pub(super) fn flags(self) -> u64 {
        match self {
            Self::ReadOnly => IO_OPEN_READ,
            Self::WriteOnly => IO_OPEN_WRITE,
            Self::ReadWrite => IO_OPEN_READ | IO_OPEN_WRITE,
            Self::ReadExecute => IO_OPEN_READ | IO_OPEN_EXECUTE,
            Self::ReadWriteExecute => IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE,
        }
    }
}

pub(super) struct Description {
    handle: Handle,
    owners: Vec<Owner>,
    inode: InodeNumber,
    access: Access,
    offset: u64,
    changes: Changes,
}

struct Changes {
    generation: FileGeneration,
    written: Option<FileGeneration>,
    truncated: Option<FileGeneration>,
    deleted: Option<FileGeneration>,
}

impl Changes {
    fn new() -> Self {
        Self {
            generation: FileGeneration(1),
            written: None,
            truncated: None,
            deleted: None,
        }
    }

    fn record(&mut self, events: FileChangeEvents) {
        self.generation.0 = self.generation.0.wrapping_add(1);
        if events.0 & IO_FILE_CHANGE_WRITE != 0 {
            self.written = Some(self.generation);
        }
        if events.0 & IO_FILE_CHANGE_TRUNCATE != 0 {
            self.truncated = Some(self.generation);
        }
        if events.0 & IO_FILE_CHANGE_DELETE != 0 {
            self.deleted = Some(self.generation);
        }
    }

    fn next_after(&self, generation: FileGeneration) -> Option<(FileGeneration, FileChangeEvents)> {
        let distance = self.generation.0.wrapping_sub(generation.0);
        let events = [
            (self.written, IO_FILE_CHANGE_WRITE),
            (self.truncated, IO_FILE_CHANGE_TRUNCATE),
            (self.deleted, IO_FILE_CHANGE_DELETE),
        ];
        let next = events
            .iter()
            .filter_map(|(recorded, _)| *recorded)
            .filter(|recorded| self.generation.0.wrapping_sub(recorded.0) < distance)
            .min_by_key(|recorded| recorded.0.wrapping_sub(generation.0))?;
        let flags = events
            .iter()
            .filter(|(recorded, _)| *recorded == Some(next))
            .fold(0, |flags, (_, event)| flags | event);
        Some((next, FileChangeEvents(flags)))
    }
}

impl Description {
    fn new(handle: Handle, owner: Owner, inode: InodeNumber, access: Access) -> Self {
        Self {
            handle,
            owners: alloc::vec![owner],
            inode,
            access,
            offset: 0,
            changes: Changes::new(),
        }
    }
}

fn validate_owner(owner: Owner) -> Result<(), Error> {
    if owner.pid.0 == 0 || owner.session.0 == 0 {
        return Err(Error::AccessDenied);
    }
    Ok(())
}

impl<D: Device> Volume<D> {
    pub(super) fn record_change(&mut self, inode: InodeNumber, events: FileChangeEvents) {
        for description in self
            .open_files
            .iter_mut()
            .filter(|description| description.inode == inode)
        {
            description.changes.record(events);
        }
    }

    pub fn watch_file(&self, owner: Owner, handle: Handle) -> Result<FileChange, Error> {
        let index = self.description_index(owner, handle)?;
        let changes = &self.open_files[index].changes;
        Ok(FileChange::new(
            FileWatch::new(handle, changes.generation),
            FileChangeEvents(0),
        ))
    }

    pub fn wait_file_change(
        &self,
        pid: ProcessId,
        watches: &[FileWatch],
    ) -> Result<Option<FileChange>, Error> {
        if watches.is_empty() || watches.len() > IO_FILE_WATCH_CAPACITY as usize {
            return Err(Error::Unsupported);
        }
        for watch in watches {
            self.watch_description_index(pid, watch.handle)?;
        }
        for watch in watches {
            let index = self.watch_description_index(pid, watch.handle)?;
            let changes = &self.open_files[index].changes;
            if let Some((generation, events)) = changes.next_after(watch.generation) {
                return Ok(Some(FileChange::new(
                    FileWatch::new(watch.handle, generation),
                    events,
                )));
            }
        }
        Ok(None)
    }

    fn watch_description_index(&self, pid: ProcessId, handle: Handle) -> Result<usize, Error> {
        if pid.0 == 0 {
            return Err(Error::AccessDenied);
        }
        self.open_files
            .iter()
            .position(|description| {
                description.handle == handle
                    && description.owners.iter().any(|owner| owner.pid == pid)
            })
            .ok_or(Error::UnknownHandle)
    }

    pub(super) fn reserve_open_file(&mut self, owner: Owner) -> Result<(), Error> {
        validate_owner(owner)?;
        if self.open_files.len() == HANDLE_LIMIT || self.next_handle == i64::MAX {
            return Err(Error::HandleLimit);
        }
        self.open_files
            .try_reserve(1)
            .map_err(|_| Error::HandleLimit)
    }

    pub(super) fn has_open_file(&self, inode: InodeNumber) -> bool {
        self.open_files
            .iter()
            .any(|description| description.inode == inode)
    }

    fn description_index(&self, owner: Owner, handle: Handle) -> Result<usize, Error> {
        validate_owner(owner)?;
        self.open_files
            .iter()
            .position(|description| {
                description.handle == handle && description.owners.contains(&owner)
            })
            .ok_or(Error::UnknownHandle)
    }

    pub fn open_file(
        &mut self,
        owner: Owner,
        path: &[u8],
        access: Access,
    ) -> Result<Handle, Error> {
        self.reserve_open_file(owner)?;
        let inode = self.lookup(path)?;
        self.open_inode(owner, &inode, access, false)
    }

    pub(super) fn open_inode(
        &mut self,
        owner: Owner,
        inode: &Inode,
        access: Access,
        truncate: bool,
    ) -> Result<Handle, Error> {
        self.reserve_open_file(owner)?;
        if !inode.is_file() {
            return Err(Error::NotFile);
        }
        if inode.links == 0 {
            return Err(Error::Corrupt);
        }
        self.validate_open_inode(inode)?;
        if truncate {
            if access.flags() & IO_OPEN_WRITE == 0 {
                return Err(Error::AccessDenied);
            }
            self.truncate(inode.number)?;
        }
        let handle = Handle(self.next_handle);
        self.open_files
            .push(Description::new(handle, owner, inode.number, access));
        self.next_handle += 1;
        Ok(handle)
    }

    pub fn read_file(
        &mut self,
        owner: Owner,
        handle: Handle,
        destination: &mut [u8],
    ) -> Result<usize, Error> {
        let index = self.description_index(owner, handle)?;
        let description = &self.open_files[index];
        if description.access.flags() & IO_OPEN_READ == 0 {
            return Err(Error::AccessDenied);
        }
        let count = self.read(description.inode, description.offset, destination)?;
        self.open_files[index].offset += count as u64;
        Ok(count)
    }

    pub fn write_file(
        &mut self,
        owner: Owner,
        handle: Handle,
        source: &[u8],
    ) -> Result<usize, Error> {
        self.write_file_at(owner, handle, source, false)
    }

    pub fn append_file(
        &mut self,
        owner: Owner,
        handle: Handle,
        source: &[u8],
    ) -> Result<usize, Error> {
        self.write_file_at(owner, handle, source, true)
    }

    fn write_file_at(
        &mut self,
        owner: Owner,
        handle: Handle,
        source: &[u8],
        append: bool,
    ) -> Result<usize, Error> {
        let index = self.description_index(owner, handle)?;
        let description = &self.open_files[index];
        if description.access.flags() & IO_OPEN_WRITE == 0 {
            return Err(Error::AccessDenied);
        }
        let inode = description.inode;
        let offset = if append {
            self.inode(inode)?.size
        } else {
            description.offset
        };
        let count = self.write(inode, offset, source)?;
        self.open_files[index].offset = offset + count as u64;
        Ok(count)
    }

    pub fn seek_file(&mut self, owner: Owner, handle: Handle, offset: u64) -> Result<(), Error> {
        let index = self.description_index(owner, handle)?;
        if offset > u64::from(MAX_FILE_BLOCKS) * BLOCK_SIZE as u64 {
            return Err(Error::FileTooLarge);
        }
        self.open_files[index].offset = offset;
        Ok(())
    }

    pub(super) fn file_offset(&self, owner: Owner, handle: Handle) -> Result<u64, Error> {
        Ok(self.open_files[self.description_index(owner, handle)?].offset)
    }

    pub(super) fn file_inode(&mut self, owner: Owner, handle: Handle) -> Result<Inode, Error> {
        let index = self.description_index(owner, handle)?;
        self.inode(self.open_files[index].inode)
    }

    pub fn close_file(&mut self, owner: Owner, handle: Handle) -> Result<(), Error> {
        let index = self.description_index(owner, handle)?;
        let remove = self.open_files[index].owners.len() == 1 && !self.grants.retains(handle);
        if remove {
            self.reclaim_description(index)?;
        }
        self.grants.close(handle, owner);
        if remove {
            self.open_files.remove(index);
        } else {
            self.open_files[index]
                .owners
                .retain(|candidate| *candidate != owner);
        }
        Ok(())
    }

    fn reclaim_description(&mut self, index: usize) -> Result<(), Error> {
        let inode = self.open_files[index].inode;
        if !self
            .open_files
            .iter()
            .enumerate()
            .any(|(candidate, description)| candidate != index && description.inode == inode)
        {
            self.reclaim_unlinked(inode)?;
        }
        Ok(())
    }

    pub fn prepare_inherit(&mut self, owner: Owner, handle: Handle) -> Result<Reply, Error> {
        let index = self.description_index(owner, handle)?;
        Ok(self
            .grants
            .prepare(handle, owner, self.open_files[index].access.flags())?)
    }

    pub fn commit_inherit(
        &mut self,
        owner: Owner,
        token: InheritanceToken,
        target: ProcessId,
    ) -> Result<(), Error> {
        validate_owner(owner)?;
        self.grants.commit(token, owner, target)?;
        Ok(())
    }

    pub fn claim_inherit(
        &mut self,
        owner: Owner,
        token: InheritanceToken,
        flags: u64,
    ) -> Result<Handle, Error> {
        validate_owner(owner)?;
        let grant = self.grants.claim(token, owner, flags)?;
        let index = self
            .open_files
            .iter()
            .position(|description| description.handle == grant.handle)
            .ok_or(Error::UnknownHandle)?;
        let already_owned = self.open_files[index].owners.contains(&owner);
        if !already_owned && self.open_files[index].owners.len() == OWNER_LIMIT {
            return Err(Error::HandleLimit);
        }
        self.grants.complete(token)?;
        if !already_owned {
            self.open_files[index].owners.push(owner);
        }
        Ok(grant.handle)
    }

    pub fn abort_inherit(&mut self, owner: Owner, token: InheritanceToken) -> Result<(), Error> {
        validate_owner(owner)?;
        let grant = self.grants.abort_candidate(token, owner)?;
        let index = self
            .open_files
            .iter()
            .position(|description| description.handle == grant.handle)
            .ok_or(Error::UnknownHandle)?;
        let remove = self.open_files[index].owners.is_empty()
            && !self.grants.retains_except(grant.handle, token);
        if remove {
            self.reclaim_description(index)?;
        }
        self.grants.abort(token, owner)?;
        if remove {
            self.open_files.remove(index);
        }
        Ok(())
    }

    pub fn release_files(&mut self, session: SessionId) -> Result<(), Error> {
        if session.0 == 0 {
            return Err(Error::AccessDenied);
        }
        while let Some((owner, handle)) = self.open_files.iter().find_map(|description| {
            description
                .owners
                .iter()
                .find(|owner| owner.session == session)
                .map(|owner| (*owner, description.handle))
        }) {
            self.close_file(owner, handle)?;
        }
        Ok(())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::Disk;
    use hunde::io::ProcessId;

    const ROOT: InodeNumber = InodeNumber(2);
    const FILE: InodeNumber = InodeNumber(12);

    fn owner(number: u64) -> Owner {
        Owner::new(SessionId(number), ProcessId(number + 100))
    }

    fn prepare(
        volume: &mut Volume<Disk>,
        source: Owner,
        handle: Handle,
    ) -> (InheritanceToken, u64) {
        match volume.prepare_inherit(source, handle).unwrap() {
            Reply::Inheritance { token, flags } => (token, flags),
            _ => panic!("Expected inheritance grant"),
        }
    }

    #[test]
    fn watches_wait_for_committed_inode_changes_across_independent_opens() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let reader = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let writer = volume
            .open_file(owner(2), b"/file", Access::ReadWrite)
            .unwrap();
        let initial = volume.watch_file(owner(1), reader).unwrap();
        assert_eq!(initial.events, FileChangeEvents(0));
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[initial.watch]),
            Ok(None)
        );
        volume.write_file(owner(2), writer, b"changed").unwrap();
        let change = volume
            .wait_file_change(owner(1).pid, &[initial.watch])
            .unwrap()
            .unwrap();
        assert_eq!(change.watch.handle, reader);
        assert_ne!(change.watch.generation, initial.watch.generation);
        assert_eq!(change.events, FileChangeEvents(IO_FILE_CHANGE_WRITE));
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[change.watch]),
            Ok(None)
        );
    }

    #[test]
    fn truncation_precedes_later_writes_without_consuming_their_read_notification() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let initial = volume.watch_file(owner(1), handle).unwrap().watch;
        volume.truncate(FILE).unwrap();
        volume.write_file(owner(1), handle, b"replacement").unwrap();
        let change = volume
            .wait_file_change(owner(1).pid, &[initial])
            .unwrap()
            .unwrap();
        assert_eq!(change.events, FileChangeEvents(IO_FILE_CHANGE_TRUNCATE));
        let written = volume
            .wait_file_change(owner(1).pid, &[change.watch])
            .unwrap()
            .unwrap();
        assert_eq!(written.events, FileChangeEvents(IO_FILE_CHANGE_WRITE));
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[written.watch]),
            Ok(None)
        );
        volume.write_file(owner(1), handle, b"more").unwrap();
        let next = volume
            .wait_file_change(owner(1).pid, &[written.watch])
            .unwrap()
            .unwrap();
        assert_eq!(next.events, FileChangeEvents(IO_FILE_CHANGE_WRITE));
    }

    #[test]
    fn hard_link_writes_and_final_unlink_keep_watches_on_the_open_inode() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let initial = volume.watch_file(owner(1), handle).unwrap().watch;
        volume.link(ROOT, b"alias", FILE).unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        assert_eq!(volume.wait_file_change(owner(1).pid, &[initial]), Ok(None));
        volume.unlink(ROOT, b"alias").unwrap();
        let deleted = volume
            .wait_file_change(owner(1).pid, &[initial])
            .unwrap()
            .unwrap();
        assert_eq!(deleted.events, FileChangeEvents(IO_FILE_CHANGE_DELETE));
        volume.create_file(ROOT, b"file", 0o644, 0, 0).unwrap();
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[deleted.watch]),
            Ok(None)
        );
        let mut data = [0; 5];
        assert_eq!(volume.read_file(owner(1), handle, &mut data), Ok(5));
        assert_eq!(&data, b"hello");
    }

    #[test]
    fn watch_batches_validate_all_owners_and_disconnect_discards_old_handles() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let initial = volume.watch_file(owner(1), handle).unwrap().watch;
        assert_eq!(
            volume.watch_file(owner(2), handle),
            Err(Error::UnknownHandle)
        );
        let (token, flags) = prepare(&mut volume, owner(1), handle);
        volume
            .commit_inherit(owner(1), token, owner(2).pid)
            .unwrap();
        assert_eq!(volume.claim_inherit(owner(2), token, flags), Ok(handle));
        assert_eq!(volume.watch_file(owner(2), handle).unwrap().watch, initial);
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[]),
            Err(Error::Unsupported)
        );
        assert_eq!(
            volume.wait_file_change(
                owner(1).pid,
                &alloc::vec![initial; IO_FILE_WATCH_CAPACITY as usize + 1]
            ),
            Err(Error::Unsupported)
        );
        volume.write_file(owner(1), handle, b"changed").unwrap();
        assert_eq!(
            volume.wait_file_change(
                owner(1).pid,
                &[initial, FileWatch::new(Handle(-1), FileGeneration(0))]
            ),
            Err(Error::UnknownHandle)
        );
        volume.release_files(owner(1).session).unwrap();
        assert_eq!(
            volume.wait_file_change(owner(1).pid, &[initial]),
            Err(Error::UnknownHandle)
        );
        assert!(
            volume
                .wait_file_change(owner(2).pid, &[initial])
                .unwrap()
                .is_some()
        );
        volume.close_file(owner(2), handle).unwrap();
    }

    #[test]
    fn failed_and_empty_writes_do_not_publish_changes() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let initial = volume.watch_file(owner(1), handle).unwrap().watch;
        assert_eq!(volume.write_file(owner(1), handle, b""), Ok(0));
        volume.device.fail_writes.push(volume.device.writes + 1);
        assert!(volume.write_file(owner(1), handle, b"failed").is_err());
        assert_eq!(volume.wait_file_change(owner(1).pid, &[initial]), Ok(None));
    }

    #[test]
    fn generation_wrap_preserves_only_actual_events() {
        let mut changes = Changes::new();
        changes.generation = FileGeneration(u64::MAX);
        changes.record(FileChangeEvents(IO_FILE_CHANGE_WRITE));
        assert_eq!(changes.generation, FileGeneration(0));
        assert_eq!(
            changes.next_after(FileGeneration(u64::MAX)),
            Some((FileGeneration(0), FileChangeEvents(IO_FILE_CHANGE_WRITE)))
        );
    }

    #[test]
    fn inherited_owners_share_offsets_seeks_access_and_writes() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let independent = volume
            .open_file(owner(2), b"/file", Access::ReadOnly)
            .unwrap();
        let mut data = [0; 2];
        volume.read_file(owner(1), handle, &mut data).unwrap();
        assert_eq!(&data, b"he");
        let (token, flags) = prepare(&mut volume, owner(1), handle);
        volume
            .commit_inherit(owner(1), token, owner(2).pid)
            .unwrap();
        assert_eq!(volume.claim_inherit(owner(2), token, flags), Ok(handle));
        volume.read_file(owner(2), handle, &mut data).unwrap();
        assert_eq!(&data, b"ll");
        volume.seek_file(owner(2), handle, 1).unwrap();
        volume.write_file(owner(1), handle, b"AB").unwrap();
        volume.read_file(owner(2), handle, &mut data).unwrap();
        assert_eq!(&data, b"lo");
        volume.read_file(owner(2), independent, &mut data).unwrap();
        assert_eq!(&data, b"hA");
        volume.close_file(owner(1), handle).unwrap();
        assert_eq!(
            volume.seek_file(owner(1), handle, 0),
            Err(Error::UnknownHandle)
        );
        volume.seek_file(owner(2), handle, 0).unwrap();
        volume.release_files(owner(1).session).unwrap();
        volume.read_file(owner(2), handle, &mut data).unwrap();
        assert_eq!(&data, b"hA");
    }

    #[test]
    fn grants_are_pid_bound_access_preserving_and_single_use() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let (token, flags) = prepare(&mut volume, owner(1), handle);
        assert_eq!(flags, IO_OPEN_READ);
        assert!(volume.claim_inherit(owner(2), token, flags).is_err());
        assert!(
            volume
                .commit_inherit(owner(2), token, owner(2).pid)
                .is_err()
        );
        assert!(
            volume
                .commit_inherit(Owner::new(SessionId(99), owner(1).pid), token, owner(2).pid)
                .is_err()
        );
        assert!(
            volume
                .commit_inherit(owner(1), token, ProcessId(0))
                .is_err()
        );
        volume
            .commit_inherit(owner(1), token, owner(2).pid)
            .unwrap();
        assert!(
            volume
                .commit_inherit(owner(1), token, owner(3).pid)
                .is_err()
        );
        assert!(volume.claim_inherit(owner(3), token, flags).is_err());
        assert!(
            volume
                .claim_inherit(owner(2), token, IO_OPEN_READ | IO_OPEN_WRITE)
                .is_err()
        );
        assert_eq!(volume.claim_inherit(owner(2), token, flags), Ok(handle));
        assert!(volume.claim_inherit(owner(2), token, flags).is_err());
        assert_eq!(
            volume.write_file(owner(2), handle, b"no"),
            Err(Error::AccessDenied)
        );
        let (second, flags) = prepare(&mut volume, owner(1), handle);
        volume
            .commit_inherit(owner(1), second, owner(2).pid)
            .unwrap();
        volume.claim_inherit(owner(2), second, flags).unwrap();
        assert_eq!(volume.open_files[0].owners.len(), 2);
        volume.close_file(owner(2), handle).unwrap();
        assert_eq!(
            volume.close_file(owner(2), handle),
            Err(Error::UnknownHandle)
        );
        assert_eq!(volume.open_files[0].owners, alloc::vec![owner(1)]);
    }

    #[test]
    fn committed_grants_pin_unlinked_files_across_source_close_and_release() {
        for disconnect in [false, true] {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let handle = volume
                .open_file(owner(1), b"/file", Access::ReadOnly)
                .unwrap();
            let (token, flags) = prepare(&mut volume, owner(1), handle);
            volume
                .commit_inherit(owner(1), token, owner(2).pid)
                .unwrap();
            volume.unlink(ROOT, b"file").unwrap();
            if disconnect {
                volume.release_files(owner(1).session).unwrap();
            } else {
                volume.close_file(owner(1), handle).unwrap();
            }
            assert!(volume.open_files[0].owners.is_empty());
            assert_eq!(volume.inode(FILE).unwrap().links, 0);
            assert_eq!(
                volume.read_file(owner(1), handle, &mut [0]),
                Err(Error::UnknownHandle)
            );
            assert_eq!(volume.claim_inherit(owner(2), token, flags), Ok(handle));
            let mut data = [0; 5];
            assert_eq!(volume.read_file(owner(2), handle, &mut data), Ok(5));
            assert_eq!(&data, b"hello");
            volume.close_file(owner(2), handle).unwrap();
            assert_eq!(volume.inode(FILE).unwrap().mode, 0);
            assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(54));
        }
    }

    #[test]
    fn uncommitted_grants_are_discarded_on_close_and_session_release() {
        for disconnect in [false, true] {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let handle = volume
                .open_file(owner(1), b"/file", Access::ReadOnly)
                .unwrap();
            let (token, flags) = prepare(&mut volume, owner(1), handle);
            volume.unlink(ROOT, b"file").unwrap();
            if disconnect {
                volume.release_files(owner(1).session).unwrap();
            } else {
                volume.close_file(owner(1), handle).unwrap();
            }
            assert!(volume.open_files.is_empty());
            assert!(volume.grants.is_empty());
            assert_eq!(volume.inode(FILE).unwrap().mode, 0);
            assert!(
                volume
                    .commit_inherit(owner(1), token, owner(2).pid)
                    .is_err()
            );
            assert!(volume.claim_inherit(owner(2), token, flags).is_err());
        }
    }

    #[test]
    fn abort_preserves_other_owners_and_other_committed_grants() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let (uncommitted, _) = prepare(&mut volume, owner(1), handle);
        assert!(volume.abort_inherit(owner(2), uncommitted).is_err());
        volume.abort_inherit(owner(1), uncommitted).unwrap();
        assert_eq!(volume.open_files.len(), 1);
        let (first, _) = prepare(&mut volume, owner(1), handle);
        let (second, flags) = prepare(&mut volume, owner(1), handle);
        volume
            .commit_inherit(owner(1), first, owner(2).pid)
            .unwrap();
        volume
            .commit_inherit(owner(1), second, owner(3).pid)
            .unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        volume.close_file(owner(1), handle).unwrap();
        volume.abort_inherit(owner(1), first).unwrap();
        assert_eq!(volume.inode(FILE).unwrap().links, 0);
        assert_eq!(volume.open_files.len(), 1);
        volume.claim_inherit(owner(3), second, flags).unwrap();
        volume.close_file(owner(3), handle).unwrap();
        assert_eq!(volume.inode(FILE).unwrap().mode, 0);
        assert!(volume.abort_inherit(owner(1), second).is_err());
    }

    #[test]
    fn inheritance_owner_and_grant_limits_do_not_consume_failed_claims() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let mut tokens = Vec::new();
        for _ in 0..64 {
            tokens.push(prepare(&mut volume, owner(1), handle).0);
        }
        assert!(volume.prepare_inherit(owner(1), handle).is_err());
        for token in tokens {
            volume.abort_inherit(owner(1), token).unwrap();
        }
        for number in 2..=OWNER_LIMIT as u64 {
            let (token, flags) = prepare(&mut volume, owner(1), handle);
            volume
                .commit_inherit(owner(1), token, owner(number).pid)
                .unwrap();
            volume.claim_inherit(owner(number), token, flags).unwrap();
        }
        let child = owner(OWNER_LIMIT as u64 + 1);
        let (token, flags) = prepare(&mut volume, owner(1), handle);
        volume.commit_inherit(owner(1), token, child.pid).unwrap();
        assert_eq!(
            volume.claim_inherit(child, token, flags),
            Err(Error::HandleLimit)
        );
        assert_eq!(volume.grants.len(), 1);
        volume.close_file(owner(2), handle).unwrap();
        assert_eq!(volume.claim_inherit(child, token, flags), Ok(handle));
        assert!(volume.grants.is_empty());
        assert_eq!(volume.open_files.len(), 1);
        assert_eq!(volume.open_files[0].owners.len(), OWNER_LIMIT);
    }

    #[test]
    fn aborted_last_grant_cleanup_failures_keep_the_token_and_pin_for_retry() {
        let prepare = || {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let handle = volume
                .open_file(owner(1), b"/file", Access::ReadOnly)
                .unwrap();
            let (token, _) = prepare(&mut volume, owner(1), handle);
            volume
                .commit_inherit(owner(1), token, owner(2).pid)
                .unwrap();
            volume.unlink(ROOT, b"file").unwrap();
            volume.close_file(owner(1), handle).unwrap();
            volume.device.writes = 0;
            (volume, token)
        };
        let (mut control, token) = prepare();
        let first = control.device.reads + 1;
        control.abort_inherit(owner(1), token).unwrap();
        for fail_at in first..=control.device.reads {
            let (mut volume, token) = prepare();
            let before = volume.device.bytes.clone();
            volume.device.fail_reads.push(fail_at);
            assert!(matches!(
                volume.abort_inherit(owner(1), token),
                Err(Error::Device(_))
            ));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.grants.len(), 1);
            assert_eq!(volume.open_files.len(), 1);
            volume.abort_inherit(owner(1), token).unwrap();
            assert!(volume.grants.is_empty());
            assert!(volume.open_files.is_empty());
        }
        for fail_at in 1..=control.device.writes {
            let (mut volume, token) = prepare();
            let before = volume.device.bytes.clone();
            volume.device.fail_writes.push(fail_at);
            assert!(matches!(
                volume.abort_inherit(owner(1), token),
                Err(Error::Device(_))
            ));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.grants.len(), 1);
            assert_eq!(volume.open_files.len(), 1);
            volume.abort_inherit(owner(1), token).unwrap();
            assert_eq!(volume.inode(FILE).unwrap().mode, 0);
        }
        let (mut volume, token) = prepare();
        volume.device.fail_writes.extend([2, 3]);
        assert_eq!(
            volume.abort_inherit(owner(1), token),
            Err(Error::RecoveryRequired)
        );
        assert_eq!(volume.grants.len(), 1);
        assert_eq!(volume.open_files.len(), 1);
    }

    #[test]
    fn failed_source_close_keeps_its_uncommitted_grant_and_independent_opens_pin_after_abort() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let (token, flags) = prepare(&mut volume, owner(1), handle);
        volume.unlink(ROOT, b"file").unwrap();
        volume.device.fail_writes.push(volume.device.writes + 2);
        assert!(matches!(
            volume.close_file(owner(1), handle),
            Err(Error::Device(_))
        ));
        assert_eq!(volume.grants.len(), 1);
        assert_eq!(volume.open_files[0].owners, alloc::vec![owner(1)]);
        volume
            .commit_inherit(owner(1), token, owner(2).pid)
            .unwrap();
        volume.close_file(owner(1), handle).unwrap();
        volume.claim_inherit(owner(2), token, flags).unwrap();
        volume.close_file(owner(2), handle).unwrap();
        assert!(volume.open_files.is_empty());

        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let other = volume
            .open_file(owner(3), b"/file", Access::ReadOnly)
            .unwrap();
        let (token, _) = prepare(&mut volume, owner(1), handle);
        volume
            .commit_inherit(owner(1), token, owner(2).pid)
            .unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        volume.close_file(owner(1), handle).unwrap();
        volume.abort_inherit(owner(1), token).unwrap();
        assert_eq!(volume.open_files.len(), 1);
        assert!(volume.grants.is_empty());
        assert_eq!(volume.inode(FILE).unwrap().links, 0);
        volume.close_file(owner(3), other).unwrap();
        assert_eq!(volume.inode(FILE).unwrap().mode, 0);
    }

    #[test]
    fn independent_opens_track_offsets_access_and_exact_owners() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let first = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let second = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let mut data = [0; 2];
        assert_eq!(volume.read_file(owner(1), first, &mut data), Ok(2));
        assert_eq!(&data, b"he");
        assert_eq!(volume.read_file(owner(1), first, &mut data), Ok(2));
        assert_eq!(&data, b"ll");
        assert_eq!(volume.read_file(owner(1), second, &mut data), Ok(2));
        assert_eq!(&data, b"he");
        assert_eq!(
            volume.write_file(owner(1), second, b"no"),
            Err(Error::AccessDenied)
        );
        for impostor in [
            owner(2),
            Owner::new(SessionId(1), ProcessId(999)),
            Owner::new(SessionId(2), owner(1).pid),
        ] {
            assert_eq!(
                volume.read_file(impostor, first, &mut data),
                Err(Error::UnknownHandle)
            );
            assert_eq!(
                volume.write_file(impostor, first, b"no"),
                Err(Error::UnknownHandle)
            );
            assert_eq!(
                volume.seek_file(impostor, first, 0),
                Err(Error::UnknownHandle)
            );
            assert_eq!(
                volume.close_file(impostor, first),
                Err(Error::UnknownHandle)
            );
        }
        assert_eq!(volume.seek_file(owner(1), first, 1), Ok(()));
        assert_eq!(volume.write_file(owner(1), first, b"AB"), Ok(2));
        assert_eq!(
            volume.seek_file(owner(1), first, u64::MAX),
            Err(Error::FileTooLarge)
        );
        assert_eq!(volume.read_file(owner(1), first, &mut data), Ok(2));
        assert_eq!(&data, b"lo");
        assert_eq!(volume.read_file(owner(1), first, &mut data), Ok(0));
        let writer = volume
            .open_file(owner(1), b"/file", Access::WriteOnly)
            .unwrap();
        assert_eq!(
            volume.read_file(owner(1), writer, &mut data),
            Err(Error::AccessDenied)
        );
        assert_eq!(volume.write_file(owner(1), writer, b"W"), Ok(1));
    }

    #[test]
    fn unlinked_files_remain_usable_and_unreusable_until_the_last_close() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let first = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        let second = volume
            .open_file(owner(2), b"/file", Access::ReadOnly)
            .unwrap();
        let before = volume.device.bytes[..5 * BLOCK_SIZE].to_vec();
        volume.unlink(ROOT, b"file").unwrap();
        assert_eq!(volume.lookup(b"/file"), Err(Error::NotFound));
        assert_eq!(volume.inode(FILE).unwrap().links, 0);
        assert_eq!(volume.device.bytes[..5 * BLOCK_SIZE], before);
        assert_eq!(volume.link(ROOT, b"resurrected", FILE), Err(Error::Corrupt));
        assert_eq!(
            volume.open_file(owner(1), b"/file", Access::ReadOnly),
            Err(Error::NotFound)
        );
        let other = volume.create_file(ROOT, b"other", 0o644, 0, 0).unwrap();
        assert_ne!(other.number, FILE);
        volume.write(other.number, 0, b"different").unwrap();
        volume
            .seek_file(owner(1), first, 268 * BLOCK_SIZE as u64)
            .unwrap();
        assert_eq!(volume.write_file(owner(1), first, b"tail"), Ok(4));
        let mut data = [0; 5];
        assert_eq!(volume.read_file(owner(2), second, &mut data), Ok(5));
        assert_eq!(&data, b"hello");
        volume.close_file(owner(1), first).unwrap();
        assert_eq!(volume.inode(FILE).unwrap().links, 0);
        volume.close_file(owner(2), second).unwrap();
        assert_eq!(volume.inode(FILE).unwrap().mode, 0);
        assert_eq!(read_u32(&volume.device.bytes, 1036), Ok(53));
        assert_eq!(read_u32(&volume.device.bytes, 1040), Ok(20));
        let reused = volume.create_file(ROOT, b"reused", 0o644, 0, 0).unwrap();
        assert_eq!(reused.number, FILE);
        assert_eq!(
            volume.read_file(owner(1), first, &mut data),
            Err(Error::UnknownHandle)
        );
        assert_eq!(
            volume.read_file(owner(2), second, &mut data),
            Err(Error::UnknownHandle)
        );
    }

    #[test]
    fn linked_files_survive_close_and_closed_handles_are_never_reused() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let first = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let before = volume.device.bytes.clone();
        volume.close_file(owner(1), first).unwrap();
        assert_eq!(volume.device.bytes, before);
        assert_eq!(
            volume.close_file(owner(1), first),
            Err(Error::UnknownHandle)
        );
        let second = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        assert_ne!(first, second);
        assert_eq!(
            volume.seek_file(owner(1), first, 0),
            Err(Error::UnknownHandle)
        );
    }

    #[test]
    fn session_release_does_not_close_another_sessions_files() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        let survivor = volume
            .open_file(owner(2), b"/file", Access::ReadOnly)
            .unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        assert_eq!(volume.release_files(SessionId(1)), Ok(()));
        assert_eq!(volume.open_files.len(), 1);
        assert_eq!(volume.inode(FILE).unwrap().links, 0);
        let mut data = [0; 5];
        assert_eq!(volume.read_file(owner(2), survivor, &mut data), Ok(5));
        assert_eq!(&data, b"hello");
        assert_eq!(volume.release_files(SessionId(2)), Ok(()));
        assert!(volume.open_files.is_empty());
        assert_eq!(volume.inode(FILE).unwrap().mode, 0);
        assert_eq!(volume.release_files(SessionId(2)), Ok(()));
    }

    #[test]
    fn opens_reject_invalid_owners_nodes_capacity_and_handle_exhaustion() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        for invalid in [
            Owner::new(SessionId(0), ProcessId(1)),
            Owner::new(SessionId(1), ProcessId(0)),
        ] {
            assert_eq!(
                volume.open_file(invalid, b"/file", Access::ReadOnly),
                Err(Error::AccessDenied)
            );
        }
        assert_eq!(
            volume.open_file(owner(1), b"/", Access::ReadOnly),
            Err(Error::NotFile)
        );
        assert_eq!(volume.release_files(SessionId(0)), Err(Error::AccessDenied));
        for _ in 0..HANDLE_LIMIT {
            volume
                .open_file(owner(1), b"/file", Access::ReadOnly)
                .unwrap();
        }
        assert_eq!(
            volume.open_file(owner(1), b"/file", Access::ReadOnly),
            Err(Error::HandleLimit)
        );
        volume.release_files(SessionId(1)).unwrap();
        volume.next_handle = i64::MAX - 1;
        let last = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        assert_eq!(last, Handle(i64::MAX - 1));
        assert_eq!(
            volume.open_file(owner(1), b"/file", Access::ReadOnly),
            Err(Error::HandleLimit)
        );
        volume.close_file(owner(1), last).unwrap();
        assert_eq!(
            volume.open_file(owner(1), b"/file", Access::ReadOnly),
            Err(Error::HandleLimit)
        );
    }

    #[test]
    fn final_close_read_and_partial_write_failures_leave_a_retryable_handle() {
        let prepare = || {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let handle = volume
                .open_file(owner(1), b"/file", Access::ReadWrite)
                .unwrap();
            volume.unlink(ROOT, b"file").unwrap();
            volume.device.writes = 0;
            (volume, handle)
        };
        let (mut control, handle) = prepare();
        let first = control.device.reads + 1;
        control.close_file(owner(1), handle).unwrap();
        for fail_at in first..=control.device.reads {
            let (mut volume, handle) = prepare();
            let before = volume.device.bytes.clone();
            volume.device.fail_reads.push(fail_at);
            assert!(matches!(
                volume.close_file(owner(1), handle),
                Err(Error::Device(_))
            ));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.open_files.len(), 1);
            volume.close_file(owner(1), handle).unwrap();
        }
        for fail_at in 1..=control.device.writes {
            let (mut volume, handle) = prepare();
            let before = volume.device.bytes.clone();
            volume.device.fail_writes.push(fail_at);
            assert!(matches!(
                volume.close_file(owner(1), handle),
                Err(Error::Device(_))
            ));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.open_files.len(), 1);
            assert_eq!(volume.write_file(owner(1), handle, b"retry"), Ok(5));
            volume.close_file(owner(1), handle).unwrap();
            assert!(volume.open_files.is_empty());
        }
    }

    #[test]
    fn cleanup_failure_remains_retryable_and_failed_rollback_is_fail_closed() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        volume.device.fail_writes.push(volume.device.writes + 2);
        assert!(matches!(
            volume.release_files(SessionId(1)),
            Err(Error::Device(_))
        ));
        assert_eq!(volume.open_files.len(), 1);
        volume.release_files(SessionId(1)).unwrap();
        assert!(volume.open_files.is_empty());
        assert_eq!(
            volume.close_file(owner(1), handle),
            Err(Error::UnknownHandle)
        );
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        volume.unlink(ROOT, b"file").unwrap();
        let next = volume.device.writes;
        volume.device.fail_writes.extend([next + 2, next + 3]);
        assert_eq!(
            volume.close_file(owner(1), handle),
            Err(Error::RecoveryRequired)
        );
        assert_eq!(volume.open_files.len(), 1);
        assert_eq!(
            volume.read_file(owner(1), handle, &mut [0]),
            Err(Error::RecoveryRequired)
        );
    }

    #[test]
    fn failed_file_io_preserves_the_description_offset() {
        let mut volume = Volume::mount(Disk::new()).unwrap();
        let handle = volume
            .open_file(owner(1), b"/file", Access::ReadWrite)
            .unwrap();
        volume.seek_file(owner(1), handle, 2).unwrap();
        volume.device.fail_reads.push(volume.device.reads + 1);
        let mut data = [0; 2];
        assert!(matches!(
            volume.read_file(owner(1), handle, &mut data),
            Err(Error::Device(_))
        ));
        assert_eq!(volume.read_file(owner(1), handle, &mut data), Ok(2));
        assert_eq!(&data, b"ll");
        volume.seek_file(owner(1), handle, 2).unwrap();
        let before = volume.device.bytes.clone();
        volume.device.fail_writes.push(volume.device.writes + 2);
        assert!(matches!(
            volume.write_file(owner(1), handle, b"X"),
            Err(Error::Device(_))
        ));
        assert_eq!(volume.device.bytes, before);
        assert_eq!(volume.write_file(owner(1), handle, b"X"), Ok(1));
        assert_eq!(volume.read_file(owner(1), handle, &mut data), Ok(2));
        assert_eq!(&data, b"lo");
    }

    #[test]
    fn failed_unlink_preserves_the_open_description_and_original_name() {
        let mut control = Volume::mount(Disk::new()).unwrap();
        control
            .open_file(owner(1), b"/file", Access::ReadOnly)
            .unwrap();
        control.unlink(ROOT, b"file").unwrap();
        for fail_at in 1..=control.device.writes {
            let mut volume = Volume::mount(Disk::new()).unwrap();
            let handle = volume
                .open_file(owner(1), b"/file", Access::ReadOnly)
                .unwrap();
            let before = volume.device.bytes.clone();
            volume.device.fail_writes.push(fail_at);
            assert!(matches!(
                volume.unlink(ROOT, b"file"),
                Err(Error::Device(_))
            ));
            assert_eq!(volume.device.bytes, before);
            assert_eq!(volume.lookup(b"/file").unwrap().links, 1);
            let mut data = [0; 5];
            assert_eq!(volume.read_file(owner(1), handle, &mut data), Ok(5));
            assert_eq!(&data, b"hello");
            volume.unlink(ROOT, b"file").unwrap();
            volume.close_file(owner(1), handle).unwrap();
            assert!(volume.open_files.is_empty());
        }
    }
}

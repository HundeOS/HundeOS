#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::vec;
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Handle, Operation, ProcessId, Reply, Request,
    SessionId, UserId,
};
use hunde::protocol::*;

const HANDLE_LIMIT: usize = 32;
const OWNER_LIMIT: usize = 64;
const SEARCH: u32 = 1;
const WRITE: u32 = 2;
const READ: u32 = 4;

#[derive(Clone, Copy, PartialEq, Eq)]
struct NodeId(u64);

struct Node {
    id: NodeId,
    path: Vec<u8>,
    directory: bool,
    owner: Credentials,
    mode: u32,
    data: Vec<u8>,
}

impl Node {
    fn new(id: NodeId, path: &[u8], directory: bool, owner: Credentials) -> Self {
        Self {
            id,
            path: path.into(),
            directory,
            owner,
            mode: if directory { 0o755 } else { 0o644 },
            data: Vec::new(),
        }
    }
    fn allows(&self, credentials: Credentials, required: u32) -> bool {
        if credentials.user == UserId(0) {
            return self.directory || required & SEARCH == 0 || self.mode & 0o111 != 0;
        }
        let shift = if credentials.user == self.owner.user {
            6
        } else if credentials.group == self.owner.group {
            3
        } else {
            0
        };
        (self.mode >> shift) & required == required
    }
    fn info(&self) -> FileInfo {
        FileInfo::new(
            (if self.directory {
                IO_MODE_TYPE_DIR
            } else {
                IO_MODE_TYPE_FILE
            }) as u32
                | self.mode,
            self.owner,
            self.data.len() as u64,
            0,
        )
    }
}

struct Description {
    handle: Handle,
    node: NodeId,
    owners: Vec<Owner>,
    flags: u64,
    offset: usize,
}

impl Description {
    fn new(handle: Handle, node: NodeId, owner: Owner, flags: u64) -> Self {
        Self {
            handle,
            node,
            owners: vec![owner],
            flags,
            offset: 0,
        }
    }
}

pub struct Memfs {
    nodes: Vec<Node>,
    descriptions: Vec<Description>,
    grants: Grants,
    next_node: u64,
    next_handle: i64,
}

impl Default for Memfs {
    fn default() -> Self {
        Self {
            nodes: Vec::new(),
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_node: 1,
            next_handle: 1 << 32,
        }
    }
}

fn parent(path: &[u8]) -> &[u8] {
    let end = path.iter().rposition(|byte| *byte == b'/').unwrap_or(0);
    if end == 0 { b"/" } else { &path[..end] }
}

fn valid_path(path: &[u8]) -> bool {
    if path == b"/" {
        return true;
    }
    path.starts_with(b"/")
        && path.len() < IO_PROTOCOL_DATA_SIZE as usize
        && !path.contains(&0)
        && path[1..]
            .split(|byte| *byte == b'/')
            .all(|part| !part.is_empty() && part != b"." && part != b"..")
}

fn descendant(candidate: &[u8], path: &[u8]) -> bool {
    candidate
        .strip_prefix(path)
        .is_some_and(|rest| rest.starts_with(b"/"))
}

impl Memfs {
    fn find(&self, path: &[u8]) -> Result<usize, IoError> {
        self.nodes
            .iter()
            .position(|node| node.path == path)
            .ok_or(IoError(IO_INVALID_PATH))
    }
    fn directory_access(
        &self,
        path: &[u8],
        credentials: Credentials,
        required: u32,
    ) -> Result<(), IoError> {
        if path == b"/" {
            return Ok(());
        }
        let node = &self.nodes[self.find(path)?];
        if !node.directory {
            return Err(IoError(IO_INVALID_PATH));
        }
        if !node.allows(credentials, required) {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(())
    }
    fn search_parents(&self, path: &[u8], credentials: Credentials) -> Result<(), IoError> {
        let mut directory = parent(path);
        while directory != b"/" {
            self.directory_access(directory, credentials, SEARCH)?;
            directory = parent(directory);
        }
        Ok(())
    }
    fn create(
        &mut self,
        path: &[u8],
        directory: bool,
        credentials: Credentials,
    ) -> Result<usize, IoError> {
        self.directory_access(parent(path), credentials, WRITE | SEARCH)?;
        if self.next_node == u64::MAX {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        self.nodes
            .try_reserve(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        let index = self.nodes.len();
        self.nodes.push(Node::new(
            NodeId(self.next_node),
            path,
            directory,
            credentials,
        ));
        self.next_node += 1;
        Ok(index)
    }
    fn open(
        &mut self,
        path: &[u8],
        flags: u64,
        owner: Owner,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        let access = flags & (IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE);
        let creation = flags & (IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE);
        if access == 0
            || flags
                & !(IO_OPEN_READ
                    | IO_OPEN_WRITE
                    | IO_OPEN_EXECUTE
                    | IO_OPEN_CREATE
                    | IO_OPEN_EXCLUSIVE
                    | IO_OPEN_TRUNCATE)
                != 0
            || (creation != 0 && (access & IO_OPEN_WRITE == 0 || creation & IO_OPEN_CREATE == 0))
            || (flags & IO_OPEN_TRUNCATE != 0 && access & IO_OPEN_WRITE == 0)
        {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        if self.descriptions.len() == HANDLE_LIMIT || self.next_handle == i64::MAX {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        if path == b"/" {
            return Err(IoError(IO_INVALID_PATH));
        }
        let index = match self.find(path) {
            Ok(index) => {
                if self.nodes[index].directory {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if flags & IO_OPEN_EXCLUSIVE != 0 {
                    return Err(IoError(IO_ALREADY_EXISTS));
                }
                index
            }
            Err(_) => {
                if flags & IO_OPEN_TRUNCATE != 0 && flags & IO_OPEN_CREATE == 0 {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if access & IO_OPEN_WRITE == 0 {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if access & IO_OPEN_EXECUTE != 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                self.create(path, false, credentials)?
            }
        };
        let required = if flags & IO_OPEN_READ != 0 { READ } else { 0 }
            | if flags & IO_OPEN_WRITE != 0 { WRITE } else { 0 }
            | if flags & IO_OPEN_EXECUTE != 0 {
                SEARCH
            } else {
                0
            };
        if !self.nodes[index].allows(credentials, required) {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        self.descriptions
            .try_reserve(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        let handle = Handle(self.next_handle);
        self.descriptions
            .push(Description::new(handle, self.nodes[index].id, owner, flags));
        self.next_handle += 1;
        if flags & IO_OPEN_TRUNCATE != 0 {
            self.nodes[index].data.clear();
        }
        Ok(Reply::Value(handle.0))
    }
    fn can_remove(&self, node: &Node, credentials: Credentials) -> Result<(), IoError> {
        self.directory_access(parent(&node.path), credentials, WRITE | SEARCH)?;
        if parent(&node.path) == b"/"
            && credentials.user != UserId(0)
            && credentials.user != node.owner.user
        {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(())
    }
    fn remove(
        &mut self,
        path: &[u8],
        flags: u64,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        if flags & !IO_REMOVE_RECURSIVE != 0 || path == b"/" {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let index = self.find(path)?;
        if self.nodes[index].directory && flags & IO_REMOVE_RECURSIVE == 0 {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        for node in self
            .nodes
            .iter()
            .filter(|node| node.path == path || descendant(&node.path, path))
        {
            self.can_remove(node, credentials)?;
        }
        let removed: Vec<NodeId> = self
            .nodes
            .iter()
            .filter(|node| node.path == path || descendant(&node.path, path))
            .map(|node| node.id)
            .collect();
        self.nodes.retain(|node| !removed.contains(&node.id));
        for description in self
            .descriptions
            .iter()
            .filter(|description| removed.contains(&description.node))
        {
            self.grants.invalidate(description.handle);
        }
        self.descriptions
            .retain(|description| !removed.contains(&description.node));
        Ok(Reply::Value(0))
    }
    fn path_operation(
        &mut self,
        owner: Owner,
        operation: Operation<'_>,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        let path = match operation {
            Operation::Open(path, _)
            | Operation::ListDirectory(path, _)
            | Operation::FileInfo(path)
            | Operation::Mkdir(path)
            | Operation::Remove(path, _) => path,
            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        };
        let path = if path.is_empty() { b"/" } else { path };
        if !valid_path(path) {
            return Err(IoError(IO_INVALID_PATH));
        }
        self.search_parents(path, credentials)?;
        match operation {
            Operation::Open(_, flags) => self.open(path, flags, owner, credentials),
            Operation::Mkdir(_) => {
                if path == b"/" || self.find(path).is_ok() {
                    return Err(IoError(IO_INVALID_PATH));
                }
                self.create(path, true, credentials)?;
                Ok(Reply::Value(0))
            }
            Operation::FileInfo(_) => {
                let info = if path == b"/" {
                    FileInfo::new(
                        IO_MODE_TYPE_DIR as u32 | 0o755,
                        Credentials::new(UserId(0), GroupId(0)),
                        0,
                        0,
                    )
                } else {
                    self.nodes[self.find(path)?].info()
                };
                Ok(Reply::FileInfo(info))
            }
            Operation::ListDirectory(_, offset) => {
                self.directory_access(path, credentials, READ | SEARCH)?;
                let skip =
                    usize::try_from(offset).map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
                let entries = self
                    .nodes
                    .iter()
                    .filter(|node| parent(&node.path) == path)
                    .skip(skip)
                    .take(IO_DIRECTORY_BATCH_CAPACITY as usize)
                    .map(|node| {
                        let start = if path == b"/" { 1 } else { path.len() + 1 };
                        DirectoryEntry::new(node.info(), &node.path[start..])
                    })
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Reply::Directory(entries))
            }
            Operation::Remove(_, flags) => self.remove(path, flags, credentials),
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }
    fn collect(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
    fn inherit(&mut self, owner: Owner, operation: Operation<'_>) -> Result<Reply, IoError> {
        match operation {
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.collect();
            }
            Operation::InheritClaim { token, flags } => {
                let grant = self.grants.claim(token, owner, flags)?;
                let description = self
                    .descriptions
                    .iter_mut()
                    .find(|description| description.handle == grant.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !description.owners.contains(&owner) {
                    if description.owners.len() == OWNER_LIMIT {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    description.owners.push(owner);
                }
                self.grants.complete(token)?;
                return Ok(Reply::Value(grant.handle.0));
            }
            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
        Ok(Reply::Value(0))
    }
    pub fn execute(
        &mut self,
        request: Request<'_>,
        credentials: impl FnOnce(ProcessId) -> Result<Credentials, IoError>,
    ) -> Result<Option<Reply>, IoError> {
        if request.pid.0 == 0 || request.session.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        match request.operation {
            Operation::Open(..)
            | Operation::ListDirectory(..)
            | Operation::FileInfo(..)
            | Operation::Mkdir(..)
            | Operation::Remove(..) => {
                let credentials = credentials(request.pid)?;
                return self
                    .path_operation(owner, request.operation, credentials)
                    .map(Some);
            }
            Operation::InheritCommit { .. }
            | Operation::InheritClaim { .. }
            | Operation::InheritAbort(_) => {
                return self.inherit(owner, request.operation).map(Some);
            }
            Operation::Release => {
                self.disconnect(request.session)?;
                return Ok(Some(Reply::Value(0)));
            }
            _ => {}
        }
        let index = self
            .descriptions
            .iter()
            .position(|description| {
                description.handle == request.handle && description.owners.contains(&owner)
            })
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        if matches!(request.operation, Operation::InheritPrepare) {
            return self
                .grants
                .prepare(request.handle, owner, self.descriptions[index].flags)
                .map(Some);
        }
        if matches!(request.operation, Operation::Close) {
            self.descriptions[index]
                .owners
                .retain(|candidate| *candidate != owner);
            self.grants.close(request.handle, owner);
            self.collect();
            return Ok(Some(Reply::Value(0)));
        }
        let description = &mut self.descriptions[index];
        let node = self
            .nodes
            .iter_mut()
            .find(|node| node.id == description.node)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        let value = match request.operation {
            Operation::Seek { offset, whence } => {
                let base = match whence {
                    IO_SEEK_SET => 0,
                    IO_SEEK_CURRENT => description.offset as i64,
                    IO_SEEK_END => node.data.len() as i64,
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                let position = base
                    .checked_add(offset)
                    .filter(|position| *position >= 0)
                    .ok_or(IoError(IO_OPERATION_NOT_SUPPORTED))?;
                description.offset =
                    usize::try_from(position).map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
                position
            }
            Operation::Read(target) => {
                if description.flags & IO_OPEN_READ == 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if request.transfer_flags & !IO_TRANSFER_NONBLOCK != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                let count = target
                    .len()
                    .min(node.data.len().saturating_sub(description.offset));
                if count != 0 {
                    target[..count].copy_from_slice(
                        &node.data[description.offset..description.offset + count],
                    );
                    description.offset += count;
                }
                count as i64
            }
            Operation::Write(source) => {
                if description.flags & IO_OPEN_WRITE == 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if request.transfer_flags & !(IO_TRANSFER_NONBLOCK | IO_TRANSFER_APPEND) != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if source.is_empty() {
                    return Ok(Some(Reply::Value(0)));
                }
                let offset = if request.transfer_flags & IO_TRANSFER_APPEND != 0 {
                    node.data.len()
                } else {
                    description.offset
                };
                let end = offset
                    .checked_add(source.len())
                    .ok_or(IoError(IO_UNKNOWN_BUFFER))?;
                if end > node.data.len() {
                    node.data
                        .try_reserve(end - node.data.len())
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    node.data.resize(end, 0);
                }
                node.data[offset..end].copy_from_slice(source);
                description.offset = end;
                source.len() as i64
            }
            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        };
        Ok(Some(Reply::Value(value)))
    }
}

impl IoComponent for Memfs {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.execute(request, hunde::runtime::credentials)
    }
    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.grants.release(session);
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.collect();
        Ok(())
    }
}

#[cfg(feature = "component")]
hunde::io_component!(Memfs, b"Rust memfs component panicked");

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::InheritanceToken;

    fn credentials(pid: ProcessId) -> Result<Credentials, IoError> {
        match pid.0 {
            1 => Ok(Credentials::new(UserId(0), GroupId(0))),
            2 => Ok(Credentials::new(UserId(1), GroupId(1))),
            3 => Ok(Credentials::new(UserId(2), GroupId(1))),
            4 => Ok(Credentials::new(UserId(3), GroupId(3))),
            _ => Err(IoError(IO_UNKNOWN_PID)),
        }
    }
    fn call(
        service: &mut Memfs,
        pid: u64,
        handle: Handle,
        operation: Operation<'_>,
    ) -> Result<Option<Reply>, IoError> {
        service.execute(
            Request::new(SessionId(pid + 100), ProcessId(pid), handle, operation),
            credentials,
        )
    }
    fn open(service: &mut Memfs, pid: u64, path: &[u8], flags: u64) -> Handle {
        match call(service, pid, Handle(0), Operation::Open(path, flags)).unwrap() {
            Some(Reply::Value(handle)) => Handle(handle),
            _ => panic!("Expected file handle"),
        }
    }
    fn seek(
        service: &mut Memfs,
        handle: Handle,
        offset: i64,
        whence: u64,
    ) -> Result<Option<Reply>, IoError> {
        call(service, 2, handle, Operation::Seek { offset, whence })
    }
    fn read_all(service: &mut Memfs, handle: Handle, size: usize) -> Vec<u8> {
        seek(service, handle, 0, IO_SEEK_SET).unwrap();
        let mut target = vec![0; size];
        call(service, 2, handle, Operation::Read(&mut target)).unwrap();
        target
    }
    fn prepare(service: &mut Memfs, handle: Handle) -> (InheritanceToken, u64) {
        match call(service, 2, handle, Operation::InheritPrepare).unwrap() {
            Some(Reply::Inheritance { token, flags }) => (token, flags),
            _ => panic!("Expected grant"),
        }
    }

    #[test]
    fn grow_binary_contents_persist_after_close_and_report_creator_metadata() {
        let mut service = Memfs::default();
        let handle = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        let source: Vec<u8> = (0..100000).map(|index| (index % 256) as u8).collect();
        assert_eq!(
            call(&mut service, 2, handle, Operation::Write(&source)),
            Ok(Some(Reply::Value(100000)))
        );
        assert_eq!(read_all(&mut service, handle, source.len()), source);
        call(&mut service, 2, handle, Operation::Close).unwrap();
        let handle = open(&mut service, 2, b"/file", IO_OPEN_READ);
        assert_eq!(read_all(&mut service, handle, source.len()), source);
        assert_eq!(
            call(&mut service, 2, Handle(0), Operation::FileInfo(b"/file")),
            Ok(Some(Reply::FileInfo(FileInfo::new(
                IO_MODE_TYPE_FILE as u32 | 0o644,
                credentials(ProcessId(2)).unwrap(),
                100000,
                0
            ))))
        );
    }

    #[test]
    fn exclusive_create_and_truncate_preserve_independent_descriptions_and_zero_fill() {
        let mut service = Memfs::default();
        let first = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        call(&mut service, 2, first, Operation::Write(b"abcdef")).unwrap();
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE)
            ),
            Err(IoError(IO_ALREADY_EXISTS))
        );
        let second = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        let mut prefix = [0; 2];
        call(&mut service, 2, second, Operation::Read(&mut prefix)).unwrap();
        assert_eq!(&prefix, b"ab");
        assert_eq!(
            seek(&mut service, first, 0, IO_SEEK_CURRENT),
            Ok(Some(Reply::Value(6)))
        );
        let truncating = open(&mut service, 2, b"/file", IO_OPEN_WRITE | IO_OPEN_TRUNCATE);
        call(&mut service, 2, truncating, Operation::Close).unwrap();
        call(&mut service, 2, first, Operation::Write(b"x")).unwrap();
        assert_eq!(read_all(&mut service, second, 7), b"\0\0\0\0\0\0x");
    }

    #[test]
    fn write_only_open_preserves_existing_data_without_explicit_truncation() {
        let mut service = Memfs::default();
        let original = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        call(&mut service, 2, original, Operation::Write(b"abc")).unwrap();
        let writer = open(&mut service, 2, b"/file", IO_OPEN_WRITE);
        call(&mut service, 2, writer, Operation::Write(b"x")).unwrap();
        let reader = open(&mut service, 2, b"/file", IO_OPEN_READ);
        assert_eq!(read_all(&mut service, reader, 3), b"xbc");
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_READ | IO_OPEN_TRUNCATE)
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
    }

    #[test]
    fn truncation_of_a_missing_file_requires_explicit_create() {
        let mut service = Memfs::default();
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(b"/missing", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        let handle = open(
            &mut service,
            2,
            b"/missing",
            IO_OPEN_WRITE | IO_OPEN_TRUNCATE | IO_OPEN_CREATE,
        );
        call(&mut service, 2, handle, Operation::Write(b"new")).unwrap();
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(
                    b"/missing",
                    IO_OPEN_WRITE | IO_OPEN_TRUNCATE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE
                )
            ),
            Err(IoError(IO_ALREADY_EXISTS))
        );
        let reader = open(&mut service, 2, b"/missing", IO_OPEN_READ);
        assert_eq!(read_all(&mut service, reader, 3), b"new");
    }

    #[test]
    fn seek_append_and_failed_growth_leave_offsets_and_data_intact() {
        let mut service = Memfs::default();
        let first = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        call(&mut service, 2, first, Operation::Write(b"abc")).unwrap();
        let second = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        let request = Request::new(
            SessionId(102),
            ProcessId(2),
            second,
            Operation::Write(b"def"),
        )
        .with_transfer_flags(IO_TRANSFER_APPEND);
        service.execute(request, credentials).unwrap();
        assert_eq!(read_all(&mut service, first, 6), b"abcdef");
        assert_eq!(
            seek(&mut service, first, -2, IO_SEEK_END),
            Ok(Some(Reply::Value(4)))
        );
        assert_eq!(
            seek(&mut service, first, -5, IO_SEEK_CURRENT),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            seek(&mut service, first, i64::MAX, IO_SEEK_SET),
            Ok(Some(Reply::Value(i64::MAX)))
        );
        assert_eq!(
            call(&mut service, 2, first, Operation::Write(b"xx")),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert_eq!(
            seek(&mut service, first, 0, IO_SEEK_CURRENT),
            Ok(Some(Reply::Value(i64::MAX)))
        );
        assert_eq!(
            seek(&mut service, first, 1, IO_SEEK_CURRENT),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(read_all(&mut service, first, 6), b"abcdef");
    }

    #[test]
    fn list_pagination_recursive_remove_and_recreation_reject_stale_handles() {
        let mut service = Memfs::default();
        call(&mut service, 2, Handle(0), Operation::Mkdir(b"/dir")).unwrap();
        let mut handles = Vec::new();
        for index in 0..5 {
            let path = alloc::format!("/dir/file{index}");
            handles.push(open(
                &mut service,
                2,
                path.as_bytes(),
                IO_OPEN_READ | IO_OPEN_WRITE,
            ));
        }
        match call(
            &mut service,
            2,
            Handle(0),
            Operation::ListDirectory(b"/dir", 0),
        )
        .unwrap()
        {
            Some(Reply::Directory(entries)) => {
                assert_eq!(entries.len(), 3);
                assert_eq!(entries[0].name(), b"file0");
            }
            _ => panic!("Expected directory batch"),
        }
        match call(
            &mut service,
            2,
            Handle(0),
            Operation::ListDirectory(b"/dir", 3),
        )
        .unwrap()
        {
            Some(Reply::Directory(entries)) => assert_eq!(entries.len(), 2),
            _ => panic!("Expected directory batch"),
        }
        let (token, flags) = prepare(&mut service, handles[0]);
        call(
            &mut service,
            2,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(3),
            },
        )
        .unwrap();
        assert_eq!(
            call(&mut service, 2, Handle(0), Operation::Remove(b"/dir", 0)),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        call(
            &mut service,
            2,
            Handle(0),
            Operation::Remove(b"/dir", IO_REMOVE_RECURSIVE),
        )
        .unwrap();
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        call(&mut service, 2, Handle(0), Operation::Mkdir(b"/dir")).unwrap();
        let replacement = open(&mut service, 2, b"/dir/file0", IO_OPEN_READ | IO_OPEN_WRITE);
        assert_ne!(replacement, handles[0]);
        assert_eq!(
            call(&mut service, 2, handles[0], Operation::Read(&mut [0])),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn path_and_handle_limits_authentication_and_disconnect_cleanup() {
        let mut service = Memfs::default();
        for path in [
            b"relative".as_slice(),
            b"/a//b",
            b"/a/../b",
            b"/a/./b",
            b"/a/",
            b"/a\0b",
        ] {
            assert_eq!(
                call(
                    &mut service,
                    2,
                    Handle(0),
                    Operation::Open(path, IO_OPEN_WRITE)
                ),
                Err(IoError(IO_INVALID_PATH))
            );
        }
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(&[b'/'; 256], IO_OPEN_WRITE)
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        for index in 0..70 {
            let name = alloc::format!("/file{index}");
            let handle = open(&mut service, 2, name.as_bytes(), IO_OPEN_WRITE);
            call(&mut service, 2, handle, Operation::Close).unwrap();
        }
        let mut handles = Vec::new();
        for _ in 0..HANDLE_LIMIT {
            handles.push(open(&mut service, 2, b"/file0", IO_OPEN_READ));
        }
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(b"/file0", IO_OPEN_READ)
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert_eq!(
            call(&mut service, 3, handles[0], Operation::Close),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        let request = Request::new(SessionId(999), ProcessId(2), handles[0], Operation::Close);
        assert_eq!(
            service.execute(request, credentials),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        service.disconnect(SessionId(102)).unwrap();
        assert!(service.descriptions.is_empty());
        assert_eq!(service.nodes.len(), 70);
    }

    #[test]
    fn owner_group_root_execute_and_sticky_root_permissions_are_preserved() {
        let mut service = Memfs::default();
        let handle = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        call(&mut service, 2, handle, Operation::Write(b"private")).unwrap();
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_WRITE)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 3, Handle(0), Operation::Remove(b"/file", 0)),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                1,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_EXECUTE)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        service.nodes[0].mode = 0o660;
        open(&mut service, 3, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        assert_eq!(
            call(
                &mut service,
                4,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_READ)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        call(&mut service, 2, Handle(0), Operation::Mkdir(b"/dir")).unwrap();
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::Open(b"/dir/child", IO_OPEN_WRITE)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(read_all(&mut service, handle, 7), b"private");
        call(&mut service, 1, Handle(0), Operation::Remove(b"/file", 0)).unwrap();
    }

    #[test]
    fn recursive_permission_denial_is_atomic_and_missing_credentials_fail_closed() {
        let mut service = Memfs::default();
        call(&mut service, 2, Handle(0), Operation::Mkdir(b"/dir")).unwrap();
        call(&mut service, 2, Handle(0), Operation::Mkdir(b"/dir/nested")).unwrap();
        let handle = open(
            &mut service,
            2,
            b"/dir/nested/child",
            IO_OPEN_READ | IO_OPEN_WRITE,
        );
        service.nodes[1].mode = 0o555;
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Remove(b"/dir", IO_REMOVE_RECURSIVE)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(service.nodes.len(), 3);
        assert_eq!(
            call(&mut service, 99, Handle(0), Operation::FileInfo(b"/dir")),
            Err(IoError(IO_UNKNOWN_PID))
        );
        let no_credentials = |_| Err(IoError(IO_UNKNOWN_PID));
        let close = Request::new(SessionId(102), ProcessId(2), handle, Operation::Close);
        assert_eq!(
            service.execute(close, no_credentials),
            Ok(Some(Reply::Value(0)))
        );
    }

    #[test]
    fn inheritance_is_single_use_pid_bound_and_shares_offsets_until_last_close() {
        let mut service = Memfs::default();
        let handle = open(&mut service, 2, b"/file", IO_OPEN_READ | IO_OPEN_WRITE);
        call(&mut service, 2, handle, Operation::Write(b"abcdef")).unwrap();
        seek(&mut service, handle, 0, IO_SEEK_SET).unwrap();
        let (token, flags) = prepare(&mut service, handle);
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        call(
            &mut service,
            2,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(3),
            },
        )
        .unwrap();
        assert_eq!(
            call(
                &mut service,
                4,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::InheritClaim {
                    token,
                    flags: IO_OPEN_READ
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Ok(Some(Reply::Value(handle.0)))
        );
        let mut prefix = [0; 2];
        call(&mut service, 3, handle, Operation::Read(&mut prefix)).unwrap();
        assert_eq!(&prefix, b"ab");
        assert_eq!(
            seek(&mut service, handle, 0, IO_SEEK_CURRENT),
            Ok(Some(Reply::Value(2)))
        );
        assert_eq!(
            call(
                &mut service,
                3,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        call(&mut service, 2, handle, Operation::Close).unwrap();
        call(&mut service, 3, handle, Operation::Read(&mut prefix)).unwrap();
        assert_eq!(&prefix, b"cd");
        service.disconnect(SessionId(103)).unwrap();
        assert!(service.descriptions.is_empty());
    }

    #[test]
    fn committed_grants_survive_source_disconnect_and_abort_reclaims_resources() {
        let mut service = Memfs::default();
        let handle = open(&mut service, 2, b"/file", IO_OPEN_WRITE);
        let (token, flags) = prepare(&mut service, handle);
        call(
            &mut service,
            2,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(3),
            },
        )
        .unwrap();
        service.disconnect(SessionId(102)).unwrap();
        assert_eq!(service.descriptions.len(), 1);
        call(
            &mut service,
            3,
            Handle(0),
            Operation::InheritClaim { token, flags },
        )
        .unwrap();
        call(&mut service, 3, handle, Operation::Write(b"child")).unwrap();
        service.disconnect(SessionId(103)).unwrap();
        assert!(service.descriptions.is_empty());
        let handle = open(&mut service, 2, b"/file", IO_OPEN_READ);
        let (token, _) = prepare(&mut service, handle);
        call(&mut service, 2, Handle(0), Operation::InheritAbort(token)).unwrap();
        assert!(service.grants.is_empty());
        service.next_handle = i64::MAX;
        assert_eq!(
            call(
                &mut service,
                2,
                Handle(0),
                Operation::Open(b"/file", IO_OPEN_READ)
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
    }
}

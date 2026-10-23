#[cfg(any(feature = "alloc", test))]
use crate::abi::{IoError, RawRequest};
#[cfg(any(feature = "alloc", test))]
use crate::protocol::{
    IO_OPERATION_NOT_IMPLEMENTED, IO_OPERATION_NOT_SUPPORTED, IO_PERMISSION_DENIED,
    IO_REQUEST_ACCEPT, IO_REQUEST_CLOSE, IO_REQUEST_FILE_INFO, IO_REQUEST_INHERIT_ABORT,
    IO_REQUEST_INHERIT_CLAIM, IO_REQUEST_INHERIT_COMMIT, IO_REQUEST_INHERIT_PREPARE,
    IO_REQUEST_LINK, IO_REQUEST_LIST_DIR, IO_REQUEST_MKDIR, IO_REQUEST_MOUNT, IO_REQUEST_OPEN,
    IO_REQUEST_PIPE_CREATE, IO_REQUEST_READ, IO_REQUEST_RELEASE_CLIENT, IO_REQUEST_REMOVE,
    IO_REQUEST_SEEK, IO_REQUEST_WATCH_FILE, IO_REQUEST_WRITE,
};

pub use crate::protocol::{
    IO_OPEN_BIND as BIND, IO_OPEN_LISTEN as LISTEN, IO_OPEN_READ as READ, IO_OPEN_WRITE as WRITE,
};

#[cfg(feature = "alloc")]
mod owned;
#[cfg(feature = "alloc")]
pub use owned::OwnedRequest;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct SessionId(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct ProcessId(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Handle(pub i64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileGeneration(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileChangeEvents(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileWatch {
    pub handle: Handle,
    pub generation: FileGeneration,
}

impl FileWatch {
    pub const fn new(handle: Handle, generation: FileGeneration) -> Self {
        Self { handle, generation }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileChange {
    pub watch: FileWatch,
    pub events: FileChangeEvents,
}

impl FileChange {
    pub const fn new(watch: FileWatch, events: FileChangeEvents) -> Self {
        Self { watch, events }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct InheritanceToken(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct UserId(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct GroupId(pub u64);

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Credentials {
    pub user: UserId,
    pub group: GroupId,
}

impl Credentials {
    pub const fn new(user: UserId, group: GroupId) -> Self {
        Self { user, group }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct FileInfo {
    pub mode: u32,
    pub owner: Credentials,
    pub size: u64,
    pub identity: u64,
}

impl FileInfo {
    pub const fn new(mode: u32, owner: Credentials, size: u64, identity: u64) -> Self {
        Self {
            mode,
            owner,
            size,
            identity,
        }
    }
}

#[cfg(feature = "alloc")]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct DirectoryEntry {
    pub info: FileInfo,
    name: alloc::vec::Vec<u8>,
}

#[cfg(feature = "alloc")]
impl DirectoryEntry {
    pub fn new(info: FileInfo, name: &[u8]) -> Result<Self, IoError> {
        if name.is_empty() || name.len() >= 256 || name.contains(&0) || name.contains(&b'/') {
            return Err(IoError(crate::protocol::IO_INVALID_PATH));
        }
        Ok(Self {
            info,
            name: name.into(),
        })
    }
    pub fn name(&self) -> &[u8] {
        &self.name
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub enum Reply {
    #[cfg(feature = "alloc")]
    Redirect(Redirect),
    Value(i64),
    Pipe {
        reader: Handle,
        writer: Handle,
    },
    Inheritance {
        token: InheritanceToken,
        flags: u64,
    },
    FileInfo(FileInfo),
    FileChange(FileChange),
    #[cfg(feature = "alloc")]
    Directory(alloc::vec::Vec<DirectoryEntry>),
}

#[cfg(feature = "alloc")]
#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Redirect {
    pub service: alloc::string::String,
    pub path: alloc::vec::Vec<u8>,
    pub target: alloc::vec::Vec<u8>,
    pub offset: u64,
}

#[cfg(feature = "alloc")]
impl Redirect {
    pub fn new(service: &str, path: &[u8], target: &[u8], offset: u64) -> Result<Self, IoError> {
        if service.is_empty()
            || service.len() >= 256
            || service.as_bytes().contains(&0)
            || path.len() >= 256
            || target.len() >= 256
            || path.contains(&0)
            || target.contains(&0)
        {
            return Err(IoError(crate::protocol::IO_INVALID_PATH));
        }
        Ok(Self {
            service: service.into(),
            path: path.into(),
            target: target.into(),
            offset,
        })
    }
}

pub enum Operation<'a> {
    Open(&'a [u8], u64),
    Accept,
    Read(&'a mut [u8]),
    Write(&'a [u8]),
    Close,
    Mount(&'a [u8]),
    Release,
    ListDirectory(&'a [u8], u64),
    FileInfo(&'a [u8]),
    WatchFile,
    #[cfg(feature = "alloc")]
    WaitFileChange(alloc::vec::Vec<FileWatch>),
    Mkdir(&'a [u8]),
    Remove(&'a [u8], u64),
    Link {
        source: &'a [u8],
        target: &'a [u8],
    },
    Seek {
        offset: i64,
        whence: u64,
    },
    PipeCreate {
        buffered: bool,
        flags: u64,
    },
    InheritPrepare,
    InheritCommit {
        token: InheritanceToken,
        target: ProcessId,
    },
    InheritClaim {
        token: InheritanceToken,
        flags: u64,
    },
    InheritAbort(InheritanceToken),
}

pub struct Request<'a> {
    pub session: SessionId,
    pub pid: ProcessId,
    pub handle: Handle,
    pub operation: Operation<'a>,
    pub transfer_flags: u64,
}

impl<'a> Request<'a> {
    pub fn new(
        session: SessionId,
        pid: ProcessId,
        handle: Handle,
        operation: Operation<'a>,
    ) -> Self {
        Self {
            session,
            pid,
            handle,
            operation,
            transfer_flags: 0,
        }
    }

    pub fn with_transfer_flags(mut self, flags: u64) -> Self {
        self.transfer_flags = flags;
        self
    }

    #[cfg(any(feature = "alloc", test))]
    pub(crate) unsafe fn decode(raw: &RawRequest) -> Result<Request<'_>, IoError> {
        let count = usize::try_from(raw.count).map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
        if raw.count > raw.capacity || count > isize::MAX as usize {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let operation = match raw.operation {
            IO_REQUEST_WATCH_FILE => Operation::WatchFile,
            #[cfg(feature = "alloc")]
            crate::protocol::IO_REQUEST_WAIT_FILE_CHANGE => {
                if count == 0
                    || !count.is_multiple_of(16)
                    || count / 16 > crate::protocol::IO_FILE_WATCH_CAPACITY as usize
                    || raw.write_data.is_null()
                {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                let bytes = unsafe { core::slice::from_raw_parts(raw.write_data, count) };
                let watches = bytes
                    .as_chunks::<16>()
                    .0
                    .iter()
                    .map(|watch| {
                        let handle = i64::from_ne_bytes(watch[..8].try_into().unwrap());
                        let generation = u64::from_ne_bytes(watch[8..].try_into().unwrap());
                        FileWatch::new(Handle(handle), FileGeneration(generation))
                    })
                    .collect();
                Operation::WaitFileChange(watches)
            }
            IO_REQUEST_PIPE_CREATE => Operation::PipeCreate {
                buffered: raw.buffered != 0,
                flags: raw.flags,
            },
            IO_REQUEST_INHERIT_PREPARE => Operation::InheritPrepare,
            IO_REQUEST_INHERIT_COMMIT => Operation::InheritCommit {
                token: InheritanceToken(raw.inheritance_token),
                target: ProcessId(raw.inheritance_pid),
            },
            IO_REQUEST_INHERIT_CLAIM => Operation::InheritClaim {
                token: InheritanceToken(raw.inheritance_token),
                flags: raw.flags,
            },
            IO_REQUEST_INHERIT_ABORT => {
                Operation::InheritAbort(InheritanceToken(raw.inheritance_token))
            }
            IO_REQUEST_OPEN | IO_REQUEST_WRITE | IO_REQUEST_LIST_DIR | IO_REQUEST_FILE_INFO
            | IO_REQUEST_MKDIR | IO_REQUEST_REMOVE | IO_REQUEST_MOUNT | IO_REQUEST_LINK => {
                let source = if count == 0 {
                    &[]
                } else {
                    if raw.write_data.is_null() {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    unsafe { core::slice::from_raw_parts(raw.write_data, count) }
                };
                match raw.operation {
                    IO_REQUEST_OPEN => Operation::Open(source, raw.flags),
                    IO_REQUEST_LIST_DIR => Operation::ListDirectory(source, raw.offset),
                    IO_REQUEST_FILE_INFO => Operation::FileInfo(source),
                    IO_REQUEST_MKDIR => Operation::Mkdir(source),
                    IO_REQUEST_REMOVE => Operation::Remove(source, raw.flags),
                    IO_REQUEST_MOUNT => Operation::Mount(source),
                    IO_REQUEST_LINK => {
                        let boundary = usize::try_from(raw.offset)
                            .map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
                        if raw.flags != 0
                            || boundary == 0
                            || boundary >= source.len()
                            || boundary >= 1024
                            || source.len() - boundary >= 1024
                        {
                            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                        }
                        let (source, target) = source.split_at(boundary);
                        if source.contains(&0) || target.contains(&0) {
                            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                        }
                        Operation::Link { source, target }
                    }
                    _ => Operation::Write(source),
                }
            }
            IO_REQUEST_ACCEPT => Operation::Accept,
            IO_REQUEST_SEEK => Operation::Seek {
                offset: raw.offset as i64,
                whence: raw.flags,
            },
            IO_REQUEST_READ => {
                let target = if count == 0 {
                    &mut []
                } else {
                    if raw.read_data.is_null() {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    unsafe { core::slice::from_raw_parts_mut(raw.read_data, count) }
                };
                Operation::Read(target)
            }
            IO_REQUEST_CLOSE => Operation::Close,
            IO_REQUEST_RELEASE_CLIENT => Operation::Release,
            _ => return Err(IoError(IO_OPERATION_NOT_IMPLEMENTED)),
        };
        let transfer_flags = match raw.operation {
            IO_REQUEST_READ | IO_REQUEST_WRITE => raw.flags,
            _ => 0,
        };
        Ok(Request::new(
            SessionId(raw.session),
            ProcessId(raw.pid),
            Handle(raw.handle),
            operation,
        )
        .with_transfer_flags(transfer_flags))
    }
}

#[cfg(test)]
mod tests {
    #[cfg(feature = "alloc")]
    #[test]
    fn watch_batches_round_trip_without_alignment_or_borrowing_assumptions() {
        use super::*;
        use crate::abi::RawRequest;
        let watches = alloc::vec![
            FileWatch::new(Handle(i64::MAX), FileGeneration(u64::MAX)),
            FileWatch::new(Handle(-1), FileGeneration(3))
        ];
        RawRequest::with_request(
            Request::new(
                SessionId(1),
                ProcessId(2),
                Handle(0),
                Operation::WaitFileChange(watches.clone()),
            ),
            |raw| {
                assert_eq!(raw.count, 32);
                let decoded = unsafe { Request::decode(raw) }.unwrap();
                match decoded.operation {
                    Operation::WaitFileChange(decoded) => assert_eq!(decoded, watches),
                    _ => panic!("Expected file watch batch"),
                }
                for length in [
                    0,
                    1,
                    15,
                    17,
                    16 * (crate::protocol::IO_FILE_WATCH_CAPACITY + 1),
                ] {
                    let invalid = RawRequest {
                        count: length,
                        capacity: length,
                        ..*raw
                    };
                    assert!(unsafe { Request::decode(&invalid) }.is_err());
                }
                let invalid = RawRequest {
                    write_data: core::ptr::null(),
                    ..*raw
                };
                assert!(unsafe { Request::decode(&invalid) }.is_err());
                let invalid = RawRequest {
                    capacity: 31,
                    ..*raw
                };
                assert!(unsafe { Request::decode(&invalid) }.is_err());
                let bytes =
                    unsafe { core::slice::from_raw_parts(raw.write_data, raw.count as usize) };
                let mut unaligned = alloc::vec![0];
                unaligned.extend_from_slice(bytes);
                let unaligned = RawRequest {
                    write_data: unaligned[1..].as_ptr(),
                    ..*raw
                };
                assert!(
                    matches!(unsafe { Request::decode(&unaligned) }.unwrap().operation, Operation::WaitFileChange(decoded) if decoded == watches)
                );
            },
        );
    }
    use super::*;

    #[test]
    fn link_decoding_checks_path_boundary_lengths_and_flags() {
        let mut raw = request(IO_REQUEST_LINK, 8);
        raw.flags = 0;
        raw.write_data = b"/from/to".as_ptr();
        raw.offset = 5;
        let decoded = unsafe { Request::decode(&raw) }.unwrap();
        assert!(matches!(
            decoded.operation,
            Operation::Link {
                source: b"/from",
                target: b"/to"
            }
        ));
        assert_eq!(decoded.transfer_flags, 0);
        for boundary in [0, 8, 9, u64::MAX] {
            raw.offset = boundary;
            assert!(unsafe { Request::decode(&raw) }.is_err());
        }
        raw.offset = 5;
        raw.flags = 1;
        assert!(unsafe { Request::decode(&raw) }.is_err());
        raw.flags = 0;
        raw.write_data = b"/fro\0/to".as_ptr();
        assert!(unsafe { Request::decode(&raw) }.is_err());
        let oversized = [b'/'; 2048];
        raw.write_data = oversized.as_ptr();
        raw.count = 2048;
        raw.capacity = 2048;
        raw.offset = 1024;
        assert!(unsafe { Request::decode(&raw) }.is_err());
    }

    #[test]
    fn open_mode_flags_do_not_become_transfer_flags_at_the_abi_boundary() {
        let mut raw = request(IO_REQUEST_OPEN, 5);
        raw.write_data = b"/file".as_ptr();
        raw.flags = crate::protocol::IO_OPEN_READ;
        let decoded = unsafe { Request::decode(&raw) }.unwrap();
        assert_eq!(decoded.transfer_flags, 0);
        assert!(matches!(
            decoded.operation,
            Operation::Open(b"/file", crate::protocol::IO_OPEN_READ)
        ));
    }

    #[test]
    fn operation_flags_remain_separate_from_transfer_flags() {
        for operation in [
            IO_REQUEST_SEEK,
            IO_REQUEST_REMOVE,
            IO_REQUEST_PIPE_CREATE,
            IO_REQUEST_INHERIT_CLAIM,
        ] {
            let mut raw = request(operation, 0);
            raw.flags = 42;
            let decoded = unsafe { Request::decode(&raw) }.unwrap();
            assert_eq!(decoded.transfer_flags, 0);
            match decoded.operation {
                Operation::Seek { whence, .. } => assert_eq!(whence, 42),
                Operation::Remove(_, flags)
                | Operation::PipeCreate { flags, .. }
                | Operation::InheritClaim { flags, .. } => assert_eq!(flags, 42),
                _ => panic!("Expected an operation with its own flags"),
            }
        }
    }

    #[test]
    fn read_and_write_preserve_all_transfer_flags_for_backend_validation() {
        for operation in [IO_REQUEST_READ, IO_REQUEST_WRITE] {
            for flags in [0, crate::protocol::IO_TRANSFER_NONBLOCK, u64::MAX] {
                let mut raw = request(operation, 0);
                raw.flags = flags;
                let decoded = unsafe { Request::decode(&raw) }.unwrap();
                assert_eq!(decoded.transfer_flags, flags);
            }
        }
    }

    #[test]
    fn new_preserves_identity_and_borrows_the_operation_buffer() {
        let mut target = [0; 2];
        let request = Request::new(
            SessionId(123),
            ProcessId(456),
            Handle(789),
            Operation::Read(&mut target),
        );
        assert_eq!(
            (request.session, request.pid, request.handle),
            (SessionId(123), ProcessId(456), Handle(789))
        );
        match request.operation {
            Operation::Read(buffer) => buffer.copy_from_slice(&[42, 255]),
            _ => panic!("Expected a read operation"),
        }
        assert_eq!(target, [42, 255]);
    }

    #[test]
    fn filesystem_operations_preserve_borrowed_paths_flags_and_signed_seek() {
        let mut raw = request(IO_REQUEST_LIST_DIR, 5);
        raw.write_data = b"/file".as_ptr();
        raw.offset = 123;
        assert!(matches!(
            unsafe { Request::decode(&raw) }.unwrap().operation,
            Operation::ListDirectory(b"/file", 123)
        ));
        raw.operation = IO_REQUEST_FILE_INFO;
        assert!(matches!(
            unsafe { Request::decode(&raw) }.unwrap().operation,
            Operation::FileInfo(b"/file")
        ));
        raw.operation = IO_REQUEST_MKDIR;
        assert!(matches!(
            unsafe { Request::decode(&raw) }.unwrap().operation,
            Operation::Mkdir(b"/file")
        ));
        raw.operation = IO_REQUEST_REMOVE;
        raw.flags = crate::protocol::IO_REMOVE_RECURSIVE;
        assert!(
            matches!(unsafe { Request::decode(&raw) }.unwrap().operation, Operation::Remove(b"/file", flags) if flags == crate::protocol::IO_REMOVE_RECURSIVE)
        );
        raw.operation = IO_REQUEST_SEEK;
        raw.count = 0;
        raw.offset = u64::MAX;
        raw.flags = crate::protocol::IO_SEEK_END;
        assert!(
            matches!(unsafe { Request::decode(&raw) }.unwrap().operation, Operation::Seek { offset: -1, whence } if whence == crate::protocol::IO_SEEK_END)
        );
    }

    fn request(operation: u32, count: u64) -> RawRequest {
        RawRequest {
            operation,
            reserved: 0,
            inheritance_token: 0,
            inheritance_pid: 0,
            buffered: 0,
            padding: 0,
            offset: 0,
            session: 1,
            pid: 2,
            handle: 3,
            flags: READ,
            count,
            read_data: core::ptr::null_mut(),
            write_data: core::ptr::null(),
            capacity: 8,
        }
    }

    #[test]
    fn empty_buffers_do_not_require_dereferenceable_pointers() {
        let raw_read = request(IO_REQUEST_READ, 0);
        let raw_write = request(IO_REQUEST_WRITE, 0);
        assert!(
            matches!(unsafe { Request::decode(&raw_read) }.unwrap().operation, Operation::Read(target) if target.is_empty())
        );
        assert!(
            matches!(unsafe { Request::decode(&raw_write) }.unwrap().operation, Operation::Write(source) if source.is_empty())
        );
    }

    #[test]
    fn invalid_spans_and_unknown_operations_are_errors() {
        for (raw, code) in [
            (request(IO_REQUEST_READ, 1), IO_PERMISSION_DENIED),
            (request(IO_REQUEST_WRITE, 1), IO_PERMISSION_DENIED),
            (request(IO_REQUEST_READ, 9), IO_OPERATION_NOT_SUPPORTED),
            (
                request(IO_REQUEST_WRITE, u64::MAX),
                IO_OPERATION_NOT_SUPPORTED,
            ),
            (request(99, 0), IO_OPERATION_NOT_IMPLEMENTED),
        ] {
            assert_eq!(unsafe { Request::decode(&raw) }.err(), Some(IoError(code)));
        }
    }

    #[test]
    fn open_translates_the_borrowed_path_flags_and_identity() {
        let mut raw = request(IO_REQUEST_OPEN, 13);
        raw.capacity = 13;
        raw.write_data = b"socket://test".as_ptr();
        let decoded = unsafe { Request::decode(&raw) }.unwrap();
        assert_eq!(
            (decoded.session, decoded.pid, decoded.handle),
            (SessionId(1), ProcessId(2), Handle(3))
        );
        assert!(
            matches!(decoded.operation, Operation::Open(path, READ) if path == b"socket://test")
        );
    }

    #[test]
    fn pipe_transfer_and_inheritance_metadata_are_preserved() {
        let mut raw = request(IO_REQUEST_PIPE_CREATE, 0);
        raw.buffered = 1;
        raw.flags = READ | WRITE;
        assert!(
            matches!(unsafe { Request::decode(&raw) }.unwrap().operation, Operation::PipeCreate { buffered: true, flags } if flags == READ | WRITE)
        );
        raw.operation = IO_REQUEST_READ;
        raw.flags = crate::protocol::IO_TRANSFER_NONBLOCK;
        assert_eq!(
            unsafe { Request::decode(&raw) }.unwrap().transfer_flags,
            crate::protocol::IO_TRANSFER_NONBLOCK
        );
        raw.operation = IO_REQUEST_INHERIT_COMMIT;
        raw.inheritance_token = 111;
        raw.inheritance_pid = 222;
        assert!(matches!(
            unsafe { Request::decode(&raw) }.unwrap().operation,
            Operation::InheritCommit {
                token: InheritanceToken(111),
                target: ProcessId(222)
            }
        ));
        raw.operation = IO_REQUEST_INHERIT_CLAIM;
        raw.flags = WRITE;
        assert!(matches!(
            unsafe { Request::decode(&raw) }.unwrap().operation,
            Operation::InheritClaim {
                token: InheritanceToken(111),
                flags: WRITE
            }
        ));
    }
}

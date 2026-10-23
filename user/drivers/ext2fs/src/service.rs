use super::*;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::Owner;
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Operation, ProcessId, Reply, Request,
    SessionId, UserId,
};
use hunde::protocol::*;

const ROOT: InodeNumber = InodeNumber(2);
const SEARCH: u16 = 1;
const WRITE: u16 = 2;
const READ: u16 = 4;

pub struct Ext2<D: Device> {
    volume: Volume<D>,
}

struct Parent<'a> {
    inode: Inode,
    name: &'a [u8],
}

impl<'a> Parent<'a> {
    fn new(inode: Inode, name: &'a [u8]) -> Self {
        Self { inode, name }
    }
}

fn io_error(error: Error) -> IoError {
    IoError(match error {
        Error::InvalidPath
        | Error::NotFound
        | Error::NotDirectory
        | Error::NotFile
        | Error::IsDirectory => IO_INVALID_PATH,
        Error::AlreadyExists => IO_ALREADY_EXISTS,
        Error::AccessDenied => IO_PERMISSION_DENIED,
        Error::UnknownHandle => IO_UNKNOWN_HANDLE,
        Error::HandleLimit | Error::NoSpace => IO_REGISTER_FAILED,
        Error::Unsupported | Error::TooManyLinks | Error::FileTooLarge | Error::NotEmpty => {
            IO_OPERATION_NOT_SUPPORTED
        }
        Error::Inheritance(error) | Error::Device(block::Error::Native(error)) => return error,
        Error::Corrupt | Error::RecoveryRequired | Error::Device(_) => IO_PANIC,
    })
}

fn info(inode: &Inode) -> FileInfo {
    FileInfo::new(
        u32::from(inode.mode),
        Credentials::new(
            UserId(u64::from(inode.user)),
            GroupId(u64::from(inode.group)),
        ),
        inode.size,
        u64::from(inode.number.0),
    )
}

fn validate_path(path: &[u8]) -> Result<(), IoError> {
    if !path.starts_with(b"/") || path.len() >= 1024 || path.contains(&0) {
        return Err(IoError(IO_INVALID_PATH));
    }
    if path
        .split(|byte| *byte == b'/')
        .filter(|name| !name.is_empty())
        .enumerate()
        .any(|(depth, name)| depth >= 32 || name.len() > 255)
    {
        return Err(IoError(IO_INVALID_PATH));
    }
    Ok(())
}

impl<D: Device> Ext2<D> {
    pub fn mount(device: D) -> Result<Self, Error> {
        Ok(Self {
            volume: Volume::mount(device)?,
        })
    }

    fn require(
        &mut self,
        inode: &Inode,
        credentials: Credentials,
        required: u16,
    ) -> Result<(), IoError> {
        if !inode.is_file() && !inode.is_directory() {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        self.volume.validate_open_inode(inode).map_err(io_error)?;
        if credentials.user == UserId(0) {
            return Ok(());
        }
        let shift = if credentials.user == UserId(u64::from(inode.user)) {
            6
        } else if credentials.group == GroupId(u64::from(inode.group)) {
            3
        } else {
            0
        };
        if (inode.mode >> shift) & required != required {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(())
    }

    fn resolve(&mut self, path: &[u8], credentials: Credentials) -> Result<Inode, IoError> {
        validate_path(path)?;
        let mut inode = self.volume.inode(ROOT).map_err(io_error)?;
        for name in path
            .split(|byte| *byte == b'/')
            .filter(|name| !name.is_empty())
        {
            if !inode.is_directory() {
                return Err(IoError(IO_INVALID_PATH));
            }
            self.require(&inode, credentials, SEARCH)?;
            if name == b"." || (name == b".." && inode.number == ROOT) {
                continue;
            }
            let entry = self
                .volume
                .entries(inode.number)
                .map_err(io_error)?
                .into_iter()
                .find(|entry| entry.name == name)
                .ok_or(IoError(IO_INVALID_PATH))?;
            inode = self.volume.inode(entry.inode).map_err(io_error)?;
        }
        if path.ends_with(b"/") && !inode.is_directory() {
            return Err(IoError(IO_INVALID_PATH));
        }
        Ok(inode)
    }

    fn parent<'a>(
        &mut self,
        path: &'a [u8],
        credentials: Credentials,
    ) -> Result<Parent<'a>, IoError> {
        validate_path(path)?;
        let boundary = path
            .iter()
            .rposition(|byte| *byte == b'/')
            .ok_or(IoError(IO_INVALID_PATH))?;
        let name = &path[boundary + 1..];
        super::write::validate_name(name).map_err(io_error)?;
        let parent = self.resolve(
            if boundary == 0 {
                b"/"
            } else {
                &path[..boundary]
            },
            credentials,
        )?;
        if !parent.is_directory() {
            return Err(IoError(IO_INVALID_PATH));
        }
        self.require(&parent, credentials, WRITE | SEARCH)?;
        Ok(Parent::new(parent, name))
    }

    fn path_operation(
        &mut self,
        owner: Owner,
        operation: Operation<'_>,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        match operation {
            Operation::Open(path, flags) => {
                let creation = flags & (IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE);
                if flags
                    & !(IO_OPEN_READ
                        | IO_OPEN_WRITE
                        | IO_OPEN_EXECUTE
                        | IO_OPEN_CREATE
                        | IO_OPEN_EXCLUSIVE
                        | IO_OPEN_TRUNCATE)
                    != 0
                    || (creation != 0
                        && (flags & IO_OPEN_WRITE == 0 || creation & IO_OPEN_CREATE == 0))
                    || (flags & IO_OPEN_TRUNCATE != 0 && flags & IO_OPEN_WRITE == 0)
                {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if flags & IO_OPEN_EXECUTE != 0 && flags & IO_OPEN_READ == 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                let access = match flags & (IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE) {
                    IO_OPEN_READ => Access::ReadOnly,
                    IO_OPEN_WRITE => Access::WriteOnly,
                    combined if combined == IO_OPEN_READ | IO_OPEN_WRITE => Access::ReadWrite,
                    combined if combined == IO_OPEN_READ | IO_OPEN_EXECUTE => Access::ReadExecute,
                    combined if combined == IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE => {
                        Access::ReadWriteExecute
                    }
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                self.volume.reserve_open_file(owner).map_err(io_error)?;
                let inode = match self.resolve(path, credentials) {
                    Ok(inode) => {
                        if flags & IO_OPEN_EXCLUSIVE != 0 {
                            return Err(IoError(IO_ALREADY_EXISTS));
                        }
                        inode
                    }
                    Err(error)
                        if error == IoError(IO_INVALID_PATH)
                            && flags & IO_OPEN_WRITE != 0
                            && flags & IO_OPEN_EXECUTE == 0
                            && (flags & IO_OPEN_TRUNCATE == 0 || flags & IO_OPEN_CREATE != 0) =>
                    {
                        let parent = self.parent(path, credentials)?;
                        if self
                            .volume
                            .entries(parent.inode.number)
                            .map_err(io_error)?
                            .iter()
                            .any(|entry| entry.name == parent.name)
                        {
                            return Err(error);
                        }
                        let user = u16::try_from(credentials.user.0)
                            .map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
                        let group = if parent.inode.mode & 0o2000 != 0 {
                            parent.inode.group
                        } else {
                            u16::try_from(credentials.group.0)
                                .map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?
                        };
                        self.volume
                            .create_file(parent.inode.number, parent.name, 0o644, user, group)
                            .map_err(io_error)?
                    }
                    Err(error) => return Err(error),
                };
                if !inode.is_file() {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if flags & IO_OPEN_EXECUTE != 0 && inode.mode & 0o111 == 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                self.require(
                    &inode,
                    credentials,
                    if flags & IO_OPEN_READ != 0 { READ } else { 0 }
                        | if flags & IO_OPEN_WRITE != 0 { WRITE } else { 0 }
                        | if flags & IO_OPEN_EXECUTE != 0 {
                            SEARCH
                        } else {
                            0
                        },
                )?;
                let handle = self
                    .volume
                    .open_inode(owner, &inode, access, flags & IO_OPEN_TRUNCATE != 0)
                    .map_err(io_error)?;
                Ok(Reply::Value(handle.0))
            }
            Operation::FileInfo(path) => {
                let inode = self.resolve(if path.is_empty() { b"/" } else { path }, credentials)?;
                self.require(&inode, credentials, 0)?;
                Ok(Reply::FileInfo(info(&inode)))
            }
            Operation::ListDirectory(path, offset) => {
                let inode = self.resolve(if path.is_empty() { b"/" } else { path }, credentials)?;
                if !inode.is_directory() {
                    return Err(IoError(IO_INVALID_PATH));
                }
                self.require(&inode, credentials, READ | SEARCH)?;
                let skip = usize::try_from(offset).map_err(|_| IoError(IO_INVALID_PATH))?;
                let mut entries = Vec::new();
                for entry in self
                    .volume
                    .entries(inode.number)
                    .map_err(io_error)?
                    .into_iter()
                    .skip(skip)
                    .take(IO_DIRECTORY_BATCH_CAPACITY as usize)
                {
                    let inode = self.volume.inode(entry.inode).map_err(io_error)?;
                    entries.push(DirectoryEntry::new(info(&inode), &entry.name)?);
                }
                Ok(Reply::Directory(entries))
            }
            Operation::Mkdir(path) => {
                let parent = self.parent(path, credentials)?;
                let user = u16::try_from(credentials.user.0)
                    .map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?;
                let group = if parent.inode.mode & 0o2000 != 0 {
                    parent.inode.group
                } else {
                    u16::try_from(credentials.group.0)
                        .map_err(|_| IoError(IO_OPERATION_NOT_SUPPORTED))?
                };
                self.volume
                    .mkdir(
                        parent.inode.number,
                        parent.name,
                        0o755 | (parent.inode.mode & 0o2000),
                        user,
                        group,
                    )
                    .map_err(io_error)?;
                Ok(Reply::Value(0))
            }
            Operation::Remove(path, flags) => {
                if flags & !IO_REMOVE_RECURSIVE != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                self.remove_path(path, flags, credentials, &mut Vec::new())?;
                Ok(Reply::Value(0))
            }
            Operation::Link { source, target } => {
                let inode = self.resolve(source, credentials)?;
                self.require(&inode, credentials, 0)?;
                let parent = self.parent(target, credentials)?;
                self.volume
                    .link(parent.inode.number, parent.name, inode.number)
                    .map_err(io_error)?;
                Ok(Reply::Value(0))
            }
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }

    fn remove_path(
        &mut self,
        path: &[u8],
        flags: u64,
        credentials: Credentials,
        ancestors: &mut Vec<InodeNumber>,
    ) -> Result<(), IoError> {
        let parent = self.parent(path, credentials)?;
        let inode = self.resolve(path, credentials)?;
        if parent.inode.mode & 0o1000 != 0
            && credentials.user.0 != 0
            && credentials.user.0 != u64::from(parent.inode.user)
            && credentials.user.0 != u64::from(inode.user)
        {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if inode.is_directory() {
            if ancestors.contains(&inode.number) || inode.number == ROOT {
                return Err(IoError(IO_PANIC));
            }
            if flags & IO_REMOVE_RECURSIVE != 0 {
                self.require(&inode, credentials, READ | SEARCH)?;
                ancestors.push(inode.number);
                for entry in self.volume.entries(inode.number).map_err(io_error)? {
                    if entry.name == b"." || entry.name == b".." {
                        continue;
                    }
                    let mut child = path.to_vec();
                    child.push(b'/');
                    child.extend_from_slice(&entry.name);
                    self.remove_path(&child, flags, credentials, ancestors)?;
                }
                ancestors.pop();
            }
            return self
                .volume
                .rmdir(parent.inode.number, parent.name)
                .map_err(io_error);
        }
        self.volume
            .unlink(parent.inode.number, parent.name)
            .map_err(io_error)
    }

    pub fn execute(
        &mut self,
        request: Request<'_>,
        credentials: impl FnOnce(ProcessId) -> Result<Credentials, IoError>,
    ) -> Result<Option<Reply>, IoError> {
        if request.pid.0 == 0 || request.session.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let supported_flags = match request.operation {
            Operation::Read(_) => IO_TRANSFER_NONBLOCK,
            Operation::Write(_) => IO_TRANSFER_NONBLOCK | IO_TRANSFER_APPEND,
            _ => 0,
        };
        if request.transfer_flags & !supported_flags != 0 {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let owner = Owner::new(request.session, request.pid);
        let reply = match request.operation {
            Operation::WatchFile => Reply::FileChange(
                self.volume
                    .watch_file(owner, request.handle)
                    .map_err(io_error)?,
            ),
            Operation::WaitFileChange(watches) => {
                return self
                    .volume
                    .wait_file_change(owner.pid, &watches)
                    .map(|change| change.map(Reply::FileChange))
                    .map_err(io_error);
            }
            operation @ (Operation::Open(..)
            | Operation::FileInfo(..)
            | Operation::ListDirectory(..)
            | Operation::Mkdir(..)
            | Operation::Remove(..)
            | Operation::Link { .. }) => {
                return self
                    .path_operation(owner, operation, credentials(request.pid)?)
                    .map(Some);
            }
            Operation::Read(destination) => Reply::Value(
                self.volume
                    .read_file(owner, request.handle, destination)
                    .map_err(io_error)? as i64,
            ),
            Operation::Write(source) => {
                let count = if request.transfer_flags & IO_TRANSFER_APPEND != 0 {
                    self.volume.append_file(owner, request.handle, source)
                } else {
                    self.volume.write_file(owner, request.handle, source)
                }
                .map_err(io_error)?;
                Reply::Value(count as i64)
            }
            Operation::Close => {
                self.volume
                    .close_file(owner, request.handle)
                    .map_err(io_error)?;
                Reply::Value(0)
            }
            Operation::Release => {
                self.volume
                    .release_files(request.session)
                    .map_err(io_error)?;
                Reply::Value(0)
            }
            Operation::Seek { offset, whence } => {
                let base = match whence {
                    IO_SEEK_SET => {
                        self.volume
                            .file_offset(owner, request.handle)
                            .map_err(io_error)?;
                        0
                    }
                    IO_SEEK_CURRENT => self
                        .volume
                        .file_offset(owner, request.handle)
                        .map_err(io_error)?,
                    IO_SEEK_END => {
                        self.volume
                            .file_inode(owner, request.handle)
                            .map_err(io_error)?
                            .size
                    }
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                let position = base
                    .checked_add_signed(offset)
                    .ok_or(IoError(IO_OPERATION_NOT_SUPPORTED))?;
                self.volume
                    .seek_file(owner, request.handle, position)
                    .map_err(io_error)?;
                Reply::Value(position as i64)
            }
            Operation::InheritPrepare => self
                .volume
                .prepare_inherit(owner, request.handle)
                .map_err(io_error)?,
            Operation::InheritCommit { token, target } => {
                self.volume
                    .commit_inherit(owner, token, target)
                    .map_err(io_error)?;
                Reply::Value(0)
            }
            Operation::InheritClaim { token, flags } => Reply::Value(
                self.volume
                    .claim_inherit(owner, token, flags)
                    .map_err(io_error)?
                    .0,
            ),
            Operation::InheritAbort(token) => {
                self.volume.abort_inherit(owner, token).map_err(io_error)?;
                Reply::Value(0)
            }
            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        };
        Ok(Some(reply))
    }
}

impl<D: Device + 'static> IoComponent for Ext2<D> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.execute(request, hunde::runtime::credentials)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.volume.release_files(session).map_err(io_error)
    }

    fn dispose(&mut self) -> Result<(), IoError> {
        if !self.volume.open_files.is_empty() || !self.volume.grants.is_empty() {
            return Err(IoError(IO_WOULD_BLOCK));
        }
        Ok(())
    }
}

impl hunde::component::FilesystemComponent for Ext2<hunde::block_host::HostedDevice> {
    fn mount(device: hunde::block_host::HostedDevice) -> Result<Self, IoError> {
        Self::mount(device).map_err(io_error)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::tests::Disk;
    use hunde::io::Handle;

    fn credentials(user: u64, group: u64) -> Credentials {
        Credentials::new(UserId(user), GroupId(group))
    }

    fn request(session: u64, handle: Handle, operation: Operation<'_>) -> Request<'_> {
        Request::new(
            SessionId(session),
            ProcessId(session + 100),
            handle,
            operation,
        )
    }

    fn open(service: &mut Ext2<Disk>, session: u64, flags: u64, identity: Credentials) -> Handle {
        match service
            .execute(
                request(session, Handle(0), Operation::Open(b"/file", flags)),
                |pid| {
                    assert_eq!(pid, ProcessId(session + 100));
                    Ok(identity)
                },
            )
            .unwrap()
        {
            Some(Reply::Value(handle)) => Handle(handle),
            _ => panic!("Expected file handle"),
        }
    }

    fn execute(
        service: &mut Ext2<Disk>,
        session: u64,
        handle: Handle,
        operation: Operation<'_>,
    ) -> Result<Option<Reply>, IoError> {
        service.execute(request(session, handle, operation), |_| {
            Err(IoError(IO_UNKNOWN_PID))
        })
    }

    #[test]
    fn watch_waits_allow_the_owning_process_on_its_dedicated_channel() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let handle = open(&mut service, 1, IO_OPEN_READ, credentials(0, 0));
        let watch = match execute(&mut service, 1, handle, Operation::WatchFile).unwrap() {
            Some(Reply::FileChange(change)) => change.watch,
            _ => panic!("Expected watch"),
        };
        let request = Request::new(
            SessionId(999),
            ProcessId(101),
            Handle(0),
            Operation::WaitFileChange(alloc::vec![watch]),
        );
        assert_eq!(
            service.execute(request, |_| Err(IoError(IO_UNKNOWN_PID))),
            Ok(None)
        );
        let request = Request::new(
            SessionId(999),
            ProcessId(102),
            Handle(0),
            Operation::WaitFileChange(alloc::vec![watch]),
        );
        assert_eq!(
            service.execute(request, |_| Err(IoError(IO_UNKNOWN_PID))),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn retained_watch_waits_complete_after_writes_and_fail_after_disconnect() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let reader = open(&mut service, 1, IO_OPEN_READ, credentials(0, 0));
        let writer = open(
            &mut service,
            2,
            IO_OPEN_READ | IO_OPEN_WRITE,
            credentials(0, 0),
        );
        let watch = match execute(&mut service, 1, reader, Operation::WatchFile).unwrap() {
            Some(Reply::FileChange(change)) => change.watch,
            _ => panic!("Expected watch"),
        };
        assert_eq!(
            execute(
                &mut service,
                1,
                reader,
                Operation::WaitFileChange(alloc::vec![watch])
            ),
            Ok(None)
        );
        assert_eq!(
            execute(&mut service, 2, writer, Operation::Write(b"grown file")),
            Ok(Some(Reply::Value(10)))
        );
        let changed = execute(
            &mut service,
            1,
            reader,
            Operation::WaitFileChange(alloc::vec![watch]),
        )
        .unwrap();
        assert!(
            matches!(changed, Some(Reply::FileChange(change)) if change.watch.handle == reader && change.events.0 == IO_FILE_CHANGE_WRITE)
        );
        hunde::component::IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
        assert_eq!(
            execute(
                &mut service,
                1,
                reader,
                Operation::WaitFileChange(alloc::vec![watch])
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn requests_enforce_credentials_and_directory_search_before_open() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        open(
            &mut service,
            1,
            IO_OPEN_READ | IO_OPEN_WRITE,
            credentials(7, 8),
        );
        open(&mut service, 2, IO_OPEN_READ, credentials(99, 8));
        assert_eq!(
            service.execute(
                request(
                    3,
                    Handle(0),
                    Operation::Open(b"/file", IO_OPEN_READ | IO_OPEN_WRITE)
                ),
                |_| Ok(credentials(99, 8))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            service.execute(
                request(3, Handle(0), Operation::Open(b"/file", IO_OPEN_READ)),
                |_| Err(IoError(IO_UNKNOWN_PID))
            ),
            Err(IoError(IO_UNKNOWN_PID))
        );
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(2), DIRECTORY | 0o700);
        let mut service = Ext2::mount(disk).unwrap();
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Open(b"/file", IO_OPEN_READ)),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        open(&mut service, 1, IO_OPEN_READ, credentials(0, 0));
    }

    #[test]
    fn handle_requests_enforce_owner_and_preserve_offsets_on_failed_seeks() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let handle = open(
            &mut service,
            1,
            IO_OPEN_READ | IO_OPEN_WRITE,
            credentials(7, 8),
        );
        let mut data = [0; 2];
        assert_eq!(
            execute(&mut service, 2, handle, Operation::Read(&mut data)),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(2)))
        );
        assert_eq!(&data, b"he");
        assert_eq!(
            execute(
                &mut service,
                1,
                handle,
                Operation::Seek {
                    offset: -1,
                    whence: IO_SEEK_CURRENT
                }
            ),
            Ok(Some(Reply::Value(1)))
        );
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Write(b"AB")),
            Ok(Some(Reply::Value(2)))
        );
        assert_eq!(
            execute(
                &mut service,
                1,
                handle,
                Operation::Seek {
                    offset: -9,
                    whence: IO_SEEK_SET
                }
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(2)))
        );
        assert_eq!(&data, b"lo");
        assert_eq!(
            execute(
                &mut service,
                1,
                handle,
                Operation::Seek {
                    offset: -2,
                    whence: IO_SEEK_END
                }
            ),
            Ok(Some(Reply::Value(3)))
        );
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Close),
            Ok(Some(Reply::Value(0)))
        );
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Close),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn metadata_and_directory_batches_use_shared_reply_types() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let reply = service
            .execute(request(1, Handle(0), Operation::FileInfo(b"/file")), |_| {
                Ok(credentials(99, 99))
            })
            .unwrap();
        assert_eq!(
            reply,
            Some(Reply::FileInfo(FileInfo::new(
                u32::from(REGULAR | 0o644),
                credentials(7, 8),
                5,
                12
            )))
        );
        let Some(Reply::Directory(entries)) = service
            .execute(
                request(1, Handle(0), Operation::ListDirectory(b"", 0)),
                |_| Ok(credentials(99, 99)),
            )
            .unwrap()
        else {
            panic!("Expected directory entries");
        };
        assert_eq!(entries.len(), 3);
        assert_eq!(entries[2].name(), b"file");
        assert_eq!(entries[2].info.identity, 12);
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::ListDirectory(b"/", 3)),
                |_| Ok(credentials(99, 99))
            ),
            Ok(Some(Reply::Directory(Vec::new())))
        );
    }

    #[test]
    fn path_walk_checks_search_even_before_dot_dot_and_clamps_root() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        service.volume.mkdir(ROOT, b"private", 0o700, 0, 0).unwrap();
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::FileInfo(b"/private/../file")),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(
            service
                .execute(
                    request(1, Handle(0), Operation::FileInfo(b"/../../file")),
                    |_| Ok(credentials(7, 8))
                )
                .is_ok()
        );
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::FileInfo(b"/file/")),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::FileInfo(b"relative")),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_INVALID_PATH))
        );
    }

    #[test]
    fn mkdir_inherits_setgid_and_remove_checks_parent_permissions_and_sticky_bit() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(2), DIRECTORY | 0o3777);
        disk.half(Disk::inode_offset(2) + 24, 8);
        let mut service = Ext2::mount(disk).unwrap();
        assert_eq!(
            service.execute(request(1, Handle(0), Operation::Mkdir(b"/child")), |_| Ok(
                credentials(7, 9)
            )),
            Ok(Some(Reply::Value(0)))
        );
        let inode = service.volume.lookup(b"/child").unwrap();
        assert_eq!(
            (inode.user, inode.group, inode.mode),
            (7, 8, DIRECTORY | 0o2755)
        );
        assert_eq!(
            service.execute(
                request(2, Handle(0), Operation::Remove(b"/child", 0)),
                |_| Ok(credentials(99, 99))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Remove(b"/child", 0)),
                |_| Ok(credentials(7, 9))
            ),
            Ok(Some(Reply::Value(0)))
        );
        assert_eq!(service.volume.lookup(b"/child"), Err(Error::NotFound));
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Mkdir(b"/overflow")),
                |_| Ok(credentials(65536, 9))
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(service.volume.device.bytes, before);
        let mut service = Ext2::mount(Disk::new()).unwrap();
        assert_eq!(
            service.execute(request(1, Handle(0), Operation::Mkdir(b"/denied")), |_| Ok(
                credentials(7, 8)
            )),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }

    #[test]
    fn request_inheritance_and_disconnect_keep_an_unlinked_file_usable() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let handle = open(&mut service, 1, IO_OPEN_READ, credentials(7, 8));
        let Some(Reply::Inheritance { token, flags }) =
            execute(&mut service, 1, handle, Operation::InheritPrepare).unwrap()
        else {
            panic!("Expected grant");
        };
        execute(
            &mut service,
            1,
            handle,
            Operation::InheritCommit {
                token,
                target: ProcessId(102),
            },
        )
        .unwrap();
        service
            .execute(
                request(1, Handle(0), Operation::Remove(b"/file", 0)),
                |_| Ok(credentials(0, 0)),
            )
            .unwrap();
        IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
        assert_eq!(
            execute(
                &mut service,
                2,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Ok(Some(Reply::Value(handle.0)))
        );
        let mut data = [0; 5];
        assert_eq!(
            execute(&mut service, 2, handle, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(5)))
        );
        assert_eq!(&data, b"hello");
        execute(&mut service, 2, Handle(0), Operation::Release).unwrap();
        assert_eq!(service.volume.inode(InodeNumber(12)).unwrap().mode, 0);
    }

    #[test]
    fn unsupported_operations_and_flags_never_mutate_the_volume() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let before = service.volume.device.bytes.clone();
        for flags in [
            0,
            IO_OPEN_READ | IO_OPEN_CREATE,
            IO_OPEN_WRITE | IO_OPEN_EXCLUSIVE,
            IO_OPEN_READ | IO_OPEN_BIND,
        ] {
            assert_eq!(
                service.execute(
                    request(1, Handle(0), Operation::Open(b"/file", flags)),
                    |_| Ok(credentials(0, 0))
                ),
                Err(IoError(IO_OPERATION_NOT_SUPPORTED))
            );
        }
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/file", !IO_REMOVE_RECURSIVE)
                ),
                |_| Ok(credentials(0, 0))
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        let mut packet = request(1, Handle(0), Operation::Mkdir(b"/ignored"));
        packet.transfer_flags = IO_TRANSFER_APPEND;
        assert_eq!(
            service.execute(packet, |_| Ok(credentials(0, 0))),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            service.execute(request(0, Handle(0), Operation::FileInfo(b"/")), |_| Ok(
                credentials(0, 0)
            )),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(service.volume.device.bytes, before);
        let deep = alloc::format!("/{}child", "./".repeat(32));
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Mkdir(deep.as_bytes())),
                |_| Ok(credentials(0, 0))
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(service.volume.device.bytes, before);
    }

    #[test]
    fn hard_link_requests_preserve_identity_and_check_both_paths() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Link {
                        source: b"/file",
                        target: b"/alias"
                    }
                ),
                |_| Ok(credentials(0, 0))
            ),
            Ok(Some(Reply::Value(0)))
        );
        let inode = service.volume.lookup(b"/alias").unwrap();
        assert_eq!((inode.number, inode.links), (InodeNumber(12), 2));
        let before = service.volume.device.bytes.clone();
        for (source, target, expected) in [
            (
                b"/file".as_slice(),
                b"/denied".as_slice(),
                IO_PERMISSION_DENIED,
            ),
            (b"/missing", b"/alias", IO_INVALID_PATH),
        ] {
            assert_eq!(
                service.execute(
                    request(1, Handle(0), Operation::Link { source, target }),
                    |_| Ok(credentials(7, 8))
                ),
                Err(IoError(expected))
            );
        }
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Link {
                        source: b"/file",
                        target: b"/alias"
                    }
                ),
                |_| Ok(credentials(0, 0))
            ),
            Err(IoError(IO_ALREADY_EXISTS))
        );
        assert_eq!(service.volume.device.bytes, before);
        service.volume.mkdir(ROOT, b"private", 0o700, 0, 0).unwrap();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Link {
                        source: b"/private/../file",
                        target: b"/alias"
                    }
                ),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert!(
            service
                .execute(
                    request(
                        1,
                        Handle(0),
                        Operation::Link {
                            source: b"/private",
                            target: b"/directory-link"
                        }
                    ),
                    |_| Ok(credentials(0, 0))
                )
                .is_err()
        );
    }

    #[test]
    fn recursive_removal_preserves_external_links_and_open_descriptions() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let directory = service.volume.mkdir(ROOT, b"tree", 0o755, 0, 0).unwrap();
        let nested = service
            .volume
            .mkdir(directory.number, b"nested", 0o755, 0, 0)
            .unwrap();
        let file = service
            .volume
            .create_file(nested.number, b"data", 0o644, 0, 0)
            .unwrap();
        service.volume.write(file.number, 0, b"retained").unwrap();
        service.volume.link(ROOT, b"outside", file.number).unwrap();
        let Some(Reply::Value(handle)) = service
            .execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(b"/tree/nested/data", IO_OPEN_READ),
                ),
                |_| Ok(credentials(0, 0)),
            )
            .unwrap()
        else {
            panic!("Expected handle");
        };
        assert!(
            service
                .execute(
                    request(1, Handle(0), Operation::Remove(b"/tree", 0)),
                    |_| Ok(credentials(0, 0))
                )
                .is_err()
        );
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE)
                ),
                |_| Ok(credentials(0, 0))
            ),
            Ok(Some(Reply::Value(0)))
        );
        assert_eq!(service.volume.lookup(b"/tree"), Err(Error::NotFound));
        assert_eq!(service.volume.lookup(b"/outside").unwrap().links, 1);
        service
            .execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/outside", IO_REMOVE_RECURSIVE),
                ),
                |_| Ok(credentials(0, 0)),
            )
            .unwrap();
        let mut data = [0; 8];
        assert_eq!(
            execute(&mut service, 1, Handle(handle), Operation::Read(&mut data)),
            Ok(Some(Reply::Value(8)))
        );
        assert_eq!(&data, b"retained");
        execute(&mut service, 1, Handle(handle), Operation::Close).unwrap();
        assert_eq!(service.volume.inode(file.number).unwrap().mode, 0);
    }

    #[test]
    fn recursive_removal_checks_nested_permissions_and_rejects_root_and_cycles() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(2), DIRECTORY | 0o777);
        let mut service = Ext2::mount(disk).unwrap();
        let tree = service.volume.mkdir(ROOT, b"tree", 0o1777, 0, 0).unwrap();
        service
            .volume
            .create_file(tree.number, b"owned", 0o644, 7, 8)
            .unwrap();
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE)
                ),
                |_| Ok(credentials(99, 99))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        for path in [b"/".as_slice(), b"/tree/.", b"/tree/.."] {
            assert!(
                service
                    .execute(
                        request(1, Handle(0), Operation::Remove(path, IO_REMOVE_RECURSIVE)),
                        |_| Ok(credentials(0, 0))
                    )
                    .is_err()
            );
        }
        assert_eq!(service.volume.device.bytes, before);
        let block = service.volume.inode(tree.number).unwrap().blocks[0] as usize;
        service
            .volume
            .device
            .entry(block * BLOCK_SIZE + 24, tree.number.0, 1000, b"cycle");
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE)
                ),
                |_| Ok(credentials(0, 0))
            ),
            Err(IoError(IO_PANIC))
        );
        assert_eq!(service.volume.device.bytes, before);
    }

    #[test]
    fn recursive_removal_retries_after_each_commit_failure() {
        fn prepare() -> Ext2<Disk> {
            let mut service = Ext2::mount(Disk::new()).unwrap();
            let tree = service.volume.mkdir(ROOT, b"tree", 0o755, 0, 0).unwrap();
            let nested = service
                .volume
                .mkdir(tree.number, b"nested", 0o755, 0, 0)
                .unwrap();
            let file = service
                .volume
                .create_file(nested.number, b"data", 0o644, 0, 0)
                .unwrap();
            service.volume.write(file.number, 0, b"data").unwrap();
            service
        }
        let mut control = prepare();
        let starting_writes = control.volume.device.writes;
        control
            .execute(
                request(
                    1,
                    Handle(0),
                    Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE),
                ),
                |_| Ok(credentials(0, 0)),
            )
            .unwrap();
        for failure in 1..=control.volume.device.writes - starting_writes {
            let mut service = prepare();
            service
                .volume
                .device
                .fail_writes
                .push(service.volume.device.writes + failure);
            assert_eq!(
                service.execute(
                    request(
                        1,
                        Handle(0),
                        Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE)
                    ),
                    |_| Ok(credentials(0, 0))
                ),
                Err(IoError(-72)),
                "write {failure}"
            );
            service
                .execute(
                    request(
                        1,
                        Handle(0),
                        Operation::Remove(b"/tree", IO_REMOVE_RECURSIVE),
                    ),
                    |_| Ok(credentials(0, 0)),
                )
                .unwrap();
            assert_eq!(service.volume.device.bytes, control.volume.device.bytes);
        }
    }

    #[test]
    fn recursive_removal_stops_at_the_path_limit_before_deleting_descendants() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let name = [b'a'; 250];
        let mut parent = ROOT;
        for _ in 0..5 {
            parent = service
                .volume
                .mkdir(parent, &name, 0o755, 0, 0)
                .unwrap()
                .number;
        }
        let mut path = alloc::vec![b'/'];
        path.extend_from_slice(&name);
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Remove(&path, IO_REMOVE_RECURSIVE)),
                |_| Ok(credentials(0, 0))
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(service.volume.device.bytes, before);
    }

    #[test]
    fn execute_opens_require_read_access_and_executable_permissions() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(12), REGULAR | 0o754);
        let mut service = Ext2::mount(disk).unwrap();
        let flags = IO_OPEN_READ | IO_OPEN_EXECUTE;
        let handle = open(&mut service, 1, flags, credentials(7, 99));
        open(&mut service, 2, flags, credentials(99, 8));
        open(&mut service, 3, flags, credentials(0, 0));
        assert_eq!(
            service.execute(
                request(4, Handle(0), Operation::Open(b"/file", flags)),
                |_| Ok(credentials(99, 99))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        let mut data = [0; 5];
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(5)))
        );
        assert_eq!(&data, b"hello");
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Write(b"denied")),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        for flags in [IO_OPEN_EXECUTE, IO_OPEN_WRITE | IO_OPEN_EXECUTE] {
            assert_eq!(
                service.execute(
                    request(4, Handle(0), Operation::Open(b"/file", flags)),
                    |_| Ok(credentials(0, 0))
                ),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let before = service.volume.device.bytes.clone();
        for user in [0, 7] {
            assert_eq!(
                service.execute(
                    request(1, Handle(0), Operation::Open(b"/file", flags)),
                    |_| Ok(credentials(user, 8))
                ),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        assert_eq!(service.volume.device.bytes, before);
        assert!(service.volume.open_files.is_empty());
    }

    #[test]
    fn execute_opens_never_create_missing_files_and_require_directory_search() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(12), REGULAR | 0o755);
        disk.half(Disk::inode_offset(2), DIRECTORY | 0o700);
        let mut service = Ext2::mount(disk).unwrap();
        let flags = IO_OPEN_READ | IO_OPEN_EXECUTE;
        assert_eq!(
            service.execute(
                request(1, Handle(0), Operation::Open(b"/file", flags)),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        let before = service.volume.device.bytes.clone();
        for flags in [
            flags,
            flags | IO_OPEN_WRITE,
            flags | IO_OPEN_WRITE | IO_OPEN_CREATE,
        ] {
            assert!(
                service
                    .execute(
                        request(1, Handle(0), Operation::Open(b"/missing", flags)),
                        |_| Ok(credentials(0, 0))
                    )
                    .is_err()
            );
        }
        assert_eq!(service.volume.device.bytes, before);
        assert!(service.volume.open_files.is_empty());
    }

    #[test]
    fn execute_permissions_survive_inheritance_without_elevating_access() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(12), REGULAR | 0o755);
        let mut service = Ext2::mount(disk).unwrap();
        for access in [IO_OPEN_READ, IO_OPEN_READ | IO_OPEN_WRITE] {
            let handle = open(&mut service, 1, access | IO_OPEN_EXECUTE, credentials(7, 8));
            let Some(Reply::Inheritance { token, flags }) =
                execute(&mut service, 1, handle, Operation::InheritPrepare).unwrap()
            else {
                panic!("Expected grant");
            };
            assert_eq!(flags, access | IO_OPEN_EXECUTE);
            execute(
                &mut service,
                1,
                handle,
                Operation::InheritCommit {
                    token,
                    target: ProcessId(102),
                },
            )
            .unwrap();
            assert_eq!(
                execute(
                    &mut service,
                    2,
                    Handle(0),
                    Operation::InheritClaim {
                        token,
                        flags: access
                    }
                ),
                Err(IoError(IO_PERMISSION_DENIED))
            );
            assert_eq!(
                execute(
                    &mut service,
                    2,
                    Handle(0),
                    Operation::InheritClaim { token, flags }
                ),
                Ok(Some(Reply::Value(handle.0)))
            );
            let mut data = [0; 1];
            execute(&mut service, 2, handle, Operation::Read(&mut data)).unwrap();
            assert_eq!(&data, b"h");
            let written = execute(&mut service, 2, handle, Operation::Write(b"X"));
            if access & IO_OPEN_WRITE == 0 {
                assert_eq!(written, Err(IoError(IO_PERMISSION_DENIED)));
            } else {
                assert_eq!(written, Ok(Some(Reply::Value(1))));
            }
        }
    }

    #[test]
    fn create_and_exclusive_opens_preserve_identity_and_setgid_ownership() {
        let mut disk = Disk::new();
        disk.half(Disk::inode_offset(2), DIRECTORY | 0o2777);
        disk.half(Disk::inode_offset(2) + 24, 8);
        let mut service = Ext2::mount(disk).unwrap();
        let packet = || {
            request(
                1,
                Handle(0),
                Operation::Open(
                    b"/created",
                    IO_OPEN_WRITE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE,
                ),
            )
        };
        let Some(Reply::Value(handle)) = service
            .execute(packet(), |_| Ok(credentials(7, 9)))
            .unwrap()
        else {
            panic!("Expected created handle");
        };
        let inode = service.volume.lookup(b"/created").unwrap();
        assert_eq!(
            (inode.user, inode.group, inode.mode, inode.size),
            (7, 8, REGULAR | 0o644, 0)
        );
        assert_eq!(
            service.execute(packet(), |_| Ok(credentials(7, 9))),
            Err(IoError(IO_ALREADY_EXISTS))
        );
        execute(
            &mut service,
            1,
            Handle(handle),
            Operation::Write(b"contents"),
        )
        .unwrap();
        service
            .execute(
                request(
                    2,
                    Handle(0),
                    Operation::Open(b"/created", IO_OPEN_WRITE | IO_OPEN_CREATE),
                ),
                |_| Ok(credentials(7, 9)),
            )
            .unwrap();
        assert_eq!(service.volume.lookup(b"/created").unwrap().size, 8);
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(3, Handle(0), Operation::Open(b"/overflow", IO_OPEN_WRITE)),
                |_| Ok(credentials(65536, 9))
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(service.volume.device.bytes, before);
    }

    #[test]
    fn truncating_open_keeps_inode_links_and_other_handle_offsets() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        service
            .volume
            .link(ROOT, b"alias", InodeNumber(12))
            .unwrap();
        let reader = open(&mut service, 1, IO_OPEN_READ, credentials(7, 8));
        execute(
            &mut service,
            1,
            reader,
            Operation::Seek {
                offset: 3,
                whence: IO_SEEK_SET,
            },
        )
        .unwrap();
        let watch = service
            .volume
            .watch_file(Owner::new(SessionId(1), ProcessId(101)), reader)
            .unwrap()
            .watch;
        let writer = open(
            &mut service,
            2,
            IO_OPEN_WRITE | IO_OPEN_TRUNCATE,
            credentials(7, 8),
        );
        let inode = service.volume.lookup(b"/alias").unwrap();
        let changed = service
            .volume
            .wait_file_change(ProcessId(101), &[watch])
            .unwrap()
            .unwrap();
        assert_eq!(
            changed.events,
            hunde::io::FileChangeEvents(IO_FILE_CHANGE_TRUNCATE)
        );
        assert_eq!(
            (inode.number, inode.size, inode.links, inode.sectors),
            (InodeNumber(12), 0, 2, 0)
        );
        assert_eq!(
            service
                .volume
                .file_offset(Owner::new(SessionId(1), ProcessId(101)), reader),
            Ok(3)
        );
        let mut data = [0; 5];
        assert_eq!(
            execute(&mut service, 1, reader, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(0)))
        );
        execute(&mut service, 2, writer, Operation::Write(b"new")).unwrap();
        assert_eq!(service.volume.lookup(b"/alias").unwrap().size, 3);
        assert_eq!(
            execute(&mut service, 2, writer, Operation::Read(&mut data)),
            Err(IoError(IO_PERMISSION_DENIED))
        );
    }

    #[test]
    fn write_access_preserves_contents_and_readonly_truncation_is_rejected() {
        for flags in [
            IO_OPEN_WRITE,
            IO_OPEN_WRITE | IO_OPEN_CREATE,
            IO_OPEN_READ | IO_OPEN_WRITE,
        ] {
            let mut service = Ext2::mount(Disk::new()).unwrap();
            let before = service.volume.device.bytes.clone();
            let handle = open(&mut service, 1, flags, credentials(7, 8));
            assert_eq!(service.volume.device.bytes, before);
            execute(&mut service, 1, handle, Operation::Write(b"x")).unwrap();
            let mut data = [0; 5];
            assert_eq!(service.volume.read(InodeNumber(12), 0, &mut data), Ok(5));
            assert_eq!(&data, b"xello");
        }
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(b"/file", IO_OPEN_READ | IO_OPEN_TRUNCATE)
                ),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(service.volume.device.bytes, before);
        assert!(service.volume.open_files.is_empty());
    }

    #[test]
    fn failed_or_denied_opens_do_not_truncate_create_or_leak_handles() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let before = service.volume.device.bytes.clone();
        for (path, flags) in [
            (b"/file".as_slice(), IO_OPEN_WRITE),
            (b"/denied", IO_OPEN_WRITE | IO_OPEN_CREATE),
        ] {
            assert_eq!(
                service.execute(request(1, Handle(0), Operation::Open(path, flags)), |_| Ok(
                    credentials(99, 99)
                )),
                Err(IoError(IO_PERMISSION_DENIED))
            );
        }
        assert_eq!(service.volume.device.bytes, before);
        service.volume.device.fail_writes.push(1);
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(b"/file", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
                ),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(-72))
        );
        assert!(service.volume.open_files.is_empty());
        assert_eq!(service.volume.device.bytes, before);
        for _ in 0..64 {
            open(&mut service, 1, IO_OPEN_READ, credentials(7, 8));
        }
        for path in [b"/file".as_slice(), b"/missing"] {
            assert_eq!(
                service.execute(
                    request(1, Handle(0), Operation::Open(path, IO_OPEN_WRITE)),
                    |_| Ok(credentials(0, 0))
                ),
                Err(IoError(IO_REGISTER_FAILED))
            );
        }
        assert_eq!(service.volume.device.bytes, before);
    }

    #[test]
    fn missing_truncation_requires_create_and_exclusive_create_never_truncates() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let before = service.volume.device.bytes.clone();
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(b"/missing", IO_OPEN_WRITE | IO_OPEN_TRUNCATE)
                ),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_INVALID_PATH))
        );
        assert_eq!(
            service.execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(
                        b"/file",
                        IO_OPEN_WRITE | IO_OPEN_TRUNCATE | IO_OPEN_CREATE | IO_OPEN_EXCLUSIVE
                    )
                ),
                |_| Ok(credentials(7, 8))
            ),
            Err(IoError(IO_ALREADY_EXISTS))
        );
        assert_eq!(service.volume.device.bytes, before);
        service
            .execute(
                request(
                    1,
                    Handle(0),
                    Operation::Open(
                        b"/missing",
                        IO_OPEN_WRITE | IO_OPEN_TRUNCATE | IO_OPEN_CREATE,
                    ),
                ),
                |_| Ok(credentials(0, 0)),
            )
            .unwrap();
        assert_eq!(service.volume.lookup(b"/missing").unwrap().size, 0);
    }

    #[test]
    fn append_uses_current_eof_and_failed_writes_preserve_offsets() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let first = open(
            &mut service,
            1,
            IO_OPEN_READ | IO_OPEN_WRITE,
            credentials(7, 8),
        );
        let second = open(
            &mut service,
            2,
            IO_OPEN_READ | IO_OPEN_WRITE,
            credentials(7, 8),
        );
        for (session, handle, data) in [(1, first, b"A"), (2, second, b"B")] {
            let packet = request(session, handle, Operation::Write(data))
                .with_transfer_flags(IO_TRANSFER_APPEND | IO_TRANSFER_NONBLOCK);
            assert_eq!(
                service.execute(packet, |_| panic!("No credential lookup for handle writes")),
                Ok(Some(Reply::Value(1)))
            );
        }
        let owner = Owner::new(SessionId(1), ProcessId(101));
        assert_eq!(service.volume.file_offset(owner, first), Ok(6));
        service
            .volume
            .device
            .fail_writes
            .push(service.volume.device.writes + 1);
        assert_eq!(
            service.execute(
                request(1, first, Operation::Write(b"C")).with_transfer_flags(IO_TRANSFER_APPEND),
                |_| panic!("No credential lookup")
            ),
            Err(IoError(-72))
        );
        assert_eq!(service.volume.file_offset(owner, first), Ok(6));
        let mut data = [0; 7];
        service.volume.read(InodeNumber(12), 0, &mut data).unwrap();
        assert_eq!(&data, b"helloAB");
        assert_eq!(
            service.execute(
                request(1, first, Operation::Read(&mut data))
                    .with_transfer_flags(IO_TRANSFER_APPEND),
                |_| panic!("No credential lookup")
            ),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
    }

    #[test]
    fn io_errors_and_failed_disconnects_are_reported_without_losing_handles() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        let handle = open(&mut service, 1, IO_OPEN_READ, credentials(7, 8));
        service.volume.unlink(ROOT, b"file").unwrap();
        service
            .volume
            .device
            .fail_writes
            .push(service.volume.device.writes + 2);
        assert_eq!(
            IoComponent::disconnect(&mut service, SessionId(1)),
            Err(IoError(-72))
        );
        let mut data = [0; 5];
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Read(&mut data)),
            Ok(Some(Reply::Value(5)))
        );
        assert_eq!(&data, b"hello");
        IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
        assert_eq!(
            execute(&mut service, 1, handle, Operation::Read(&mut data)),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(io_error(Error::RecoveryRequired), IoError(IO_PANIC));
    }

    #[test]
    fn disposal_requires_no_open_owners_or_retained_grants() {
        let mut service = Ext2::mount(Disk::new()).unwrap();
        assert_eq!(IoComponent::dispose(&mut service), Ok(()));
        let handle = open(&mut service, 1, IO_OPEN_READ, credentials(7, 8));
        assert_eq!(
            IoComponent::dispose(&mut service),
            Err(IoError(IO_WOULD_BLOCK))
        );
        let Some(Reply::Inheritance { token, .. }) =
            execute(&mut service, 1, handle, Operation::InheritPrepare).unwrap()
        else {
            panic!("Expected grant");
        };
        execute(
            &mut service,
            1,
            handle,
            Operation::InheritCommit {
                token,
                target: ProcessId(102),
            },
        )
        .unwrap();
        service.volume.unlink(ROOT, b"file").unwrap();
        IoComponent::disconnect(&mut service, SessionId(1)).unwrap();
        assert_eq!(
            IoComponent::dispose(&mut service),
            Err(IoError(IO_WOULD_BLOCK))
        );
        execute(&mut service, 1, Handle(0), Operation::InheritAbort(token)).unwrap();
        assert_eq!(IoComponent::dispose(&mut service), Ok(()));
    }
}

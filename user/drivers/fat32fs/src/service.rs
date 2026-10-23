use super::*;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{
    Credentials, DirectoryEntry, FileInfo, GroupId, Handle, Operation, ProcessId, Reply, Request,
    SessionId, UserId,
};
use hunde::protocol::*;

const HANDLE_LIMIT: usize = 64;
const OWNER_LIMIT: usize = 64;

struct Description {
    handle: Handle,
    path: String,
    owners: Vec<Owner>,
    flags: u64,
    offset: u64,
    location: DirectorySlot,
}

impl Description {
    fn new(
        handle: Handle,
        path: String,
        owners: Vec<Owner>,
        flags: u64,
        location: DirectorySlot,
    ) -> Self {
        Self {
            handle,
            path,
            owners,
            flags,
            offset: 0,
            location,
        }
    }
}

pub struct Fat32<D: Device> {
    volume: Volume<D>,
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

fn io_error(error: Error) -> IoError {
    IoError(match error {
        Error::InvalidPath | Error::NotFound | Error::NotDirectory | Error::NotFile => {
            IO_INVALID_PATH
        }
        Error::AlreadyExists => IO_ALREADY_EXISTS,
        Error::AccessDenied => IO_PERMISSION_DENIED,
        Error::OutOfMemory | Error::NoSpace => IO_REGISTER_FAILED,
        Error::Unsupported | Error::FileTooLarge | Error::NotEmpty => IO_OPERATION_NOT_SUPPORTED,
        Error::Device(block::Error::Native(error)) => return error,
        Error::Corrupt | Error::RecoveryRequired | Error::Device(_) => IO_PANIC,
    })
}

fn path(bytes: &[u8]) -> Result<String, IoError> {
    let text = core::str::from_utf8(bytes).map_err(|_| IoError(IO_INVALID_PATH))?;
    if text.contains('\0') {
        return Err(IoError(IO_INVALID_PATH));
    }
    super::validate_path(text).map_err(io_error)?;
    let mut result = String::new();
    result
        .try_reserve(text.len())
        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
    result.extend(text.chars().map(|character| character.to_ascii_uppercase()));
    Ok(result)
}

fn info(entry: &Entry) -> FileInfo {
    let mode = match entry.kind {
        Kind::File => IO_MODE_TYPE_FILE as u32 | 0o755,
        Kind::Directory => IO_MODE_TYPE_DIR as u32 | 0o755,
    };
    FileInfo::new(
        if entry.attributes & 1 != 0 {
            mode & !0o222
        } else {
            mode
        },
        Credentials::new(UserId(0), GroupId(0)),
        u64::from(entry.size),
        entry.location.map_or(0, |slot| {
            slot.sector.0 * SECTOR_SIZE as u64 + slot.offset as u64
        }),
    )
}

impl<D: Device> Fat32<D> {
    pub fn mount(device: D) -> Result<Self, Error> {
        Ok(Self {
            volume: Volume::mount(device)?,
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: 1 << 32,
        })
    }

    fn index(&self, owner: Owner, handle: Handle) -> Result<usize, IoError> {
        self.descriptions
            .iter()
            .position(|description| {
                description.handle == handle && description.owners.contains(&owner)
            })
            .ok_or(IoError(IO_UNKNOWN_HANDLE))
    }

    fn collect(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }

    fn release(&mut self, session: SessionId) {
        self.grants.release(session);
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.collect();
    }

    fn open(
        &mut self,
        owner: Owner,
        path: String,
        flags: u64,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        let allowed = IO_OPEN_READ
            | IO_OPEN_WRITE
            | IO_OPEN_EXECUTE
            | IO_OPEN_CREATE
            | IO_OPEN_EXCLUSIVE
            | IO_OPEN_TRUNCATE;
        let access = flags & (IO_OPEN_READ | IO_OPEN_WRITE | IO_OPEN_EXECUTE);
        if flags & IO_OPEN_EXECUTE != 0 && flags & IO_OPEN_READ == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if flags & !allowed != 0
            || access & (IO_OPEN_READ | IO_OPEN_WRITE) == 0
            || (flags & (IO_OPEN_CREATE | IO_OPEN_TRUNCATE) != 0 && flags & IO_OPEN_WRITE == 0)
            || (flags & IO_OPEN_EXCLUSIVE != 0 && flags & IO_OPEN_CREATE == 0)
        {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        if flags & IO_OPEN_WRITE != 0 && credentials.user != UserId(0) {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if self.descriptions.len() >= HANDLE_LIMIT || self.next_handle == i64::MAX {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        self.descriptions
            .try_reserve(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        let mut owners = Vec::new();
        owners
            .try_reserve(1)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        owners.push(owner);
        match self.volume.entry(&path) {
            Ok(entry) => {
                if flags & IO_OPEN_EXCLUSIVE != 0 {
                    return Err(IoError(IO_ALREADY_EXISTS));
                }
                if entry.kind != Kind::File {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if flags & IO_OPEN_WRITE != 0 && entry.attributes & 1 != 0 {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if flags & IO_OPEN_TRUNCATE != 0 {
                    self.volume.truncate(&path).map_err(io_error)?;
                }
            }
            Err(Error::NotFound)
                if flags & IO_OPEN_WRITE != 0
                    && flags & IO_OPEN_EXECUTE == 0
                    && (flags & IO_OPEN_TRUNCATE == 0 || flags & IO_OPEN_CREATE != 0) =>
            {
                self.volume.create_file(&path).map_err(io_error)?;
            }
            Err(error) => return Err(io_error(error)),
        }
        let handle = Handle(self.next_handle);
        let location = self
            .volume
            .entry(&path)
            .map_err(io_error)?
            .location
            .ok_or(IoError(IO_INVALID_PATH))?;
        self.next_handle += 1;
        self.descriptions
            .push(Description::new(handle, path, owners, access, location));
        Ok(Reply::Value(handle.0))
    }

    fn path_operation(
        &mut self,
        owner: Owner,
        operation: Operation<'_>,
        credentials: Credentials,
    ) -> Result<Reply, IoError> {
        match operation {
            Operation::Open(bytes, flags) => self.open(owner, path(bytes)?, flags, credentials),
            Operation::FileInfo(bytes) => {
                let entry = self.volume.entry(&path(bytes)?).map_err(io_error)?;
                Ok(Reply::FileInfo(info(&entry)))
            }
            Operation::ListDirectory(bytes, offset) => {
                let entries = self
                    .volume
                    .list(&path(bytes)?, offset, IO_DIRECTORY_BATCH_CAPACITY as usize)
                    .map_err(io_error)?;
                let entries = entries
                    .iter()
                    .map(|entry| DirectoryEntry::new(info(entry), entry.name.as_bytes()))
                    .collect::<Result<Vec<_>, _>>()?;
                Ok(Reply::Directory(entries))
            }
            Operation::Mkdir(bytes) => {
                if credentials.user != UserId(0) {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                self.volume
                    .create_directory(&path(bytes)?)
                    .map_err(io_error)?;
                Ok(Reply::Value(0))
            }
            Operation::Remove(bytes, flags) => {
                if credentials.user != UserId(0) {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if flags & !IO_REMOVE_RECURSIVE != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                let path = path(bytes)?;
                self.remove(&path, flags, &mut Vec::new())?;
                Ok(Reply::Value(0))
            }
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }

    fn remove(
        &mut self,
        path: &str,
        flags: u64,
        ancestors: &mut Vec<Cluster>,
    ) -> Result<(), IoError> {
        let entry = self.volume.entry(path).map_err(io_error)?;
        let location = entry.location.ok_or(IoError(IO_PERMISSION_DENIED))?;
        if entry.attributes & 1 != 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if entry.kind == Kind::File {
            if self
                .descriptions
                .iter()
                .any(|description| description.location == location)
            {
                return Err(IoError(IO_WOULD_BLOCK));
            }
            return self.volume.remove_file(path).map_err(io_error);
        }
        if self.descriptions.iter().any(|description| {
            description
                .path
                .strip_prefix(path)
                .is_some_and(|suffix| suffix.starts_with('/'))
        }) {
            return Err(IoError(IO_WOULD_BLOCK));
        }
        if flags & IO_REMOVE_RECURSIVE != 0 {
            if ancestors.contains(&entry.cluster) {
                return Err(IoError(IO_PANIC));
            }
            ancestors
                .try_reserve(1)
                .map_err(|_| IoError(IO_REGISTER_FAILED))?;
            ancestors.push(entry.cluster);
            loop {
                let children = self.volume.list(path, 0, 16).map_err(io_error)?;
                if children.is_empty() {
                    break;
                }
                for child in children {
                    let mut child_path = String::new();
                    child_path
                        .try_reserve(path.len() + child.name.len() + 1)
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    child_path.push_str(path);
                    child_path.push('/');
                    child_path.push_str(&child.name);
                    self.remove(&child_path, flags, ancestors)?;
                }
            }
            ancestors.pop();
        }
        self.volume.remove_directory(path).map_err(io_error)
    }

    pub fn execute(
        &mut self,
        request: Request<'_>,
        credentials: impl FnOnce(ProcessId) -> Result<Credentials, IoError>,
    ) -> Result<Option<Reply>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let allowed = match request.operation {
            Operation::Read(_) => IO_TRANSFER_NONBLOCK,
            Operation::Write(_) => IO_TRANSFER_NONBLOCK | IO_TRANSFER_APPEND,
            _ => 0,
        };
        if request.transfer_flags & !allowed != 0 {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let owner = Owner::new(request.session, request.pid);
        match request.operation {
            operation @ (Operation::Open(..)
            | Operation::FileInfo(..)
            | Operation::ListDirectory(..)
            | Operation::Mkdir(..)
            | Operation::Remove(..)) => {
                return self
                    .path_operation(owner, operation, credentials(request.pid)?)
                    .map(Some);
            }
            Operation::Release => {
                self.release(request.session);
                return Ok(Some(Reply::Value(0)));
            }
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?;
                return Ok(Some(Reply::Value(0)));
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.collect();
                return Ok(Some(Reply::Value(0)));
            }
            Operation::InheritClaim { token, flags } => {
                let grant = self.grants.claim(token, owner, flags)?;
                let description = self
                    .descriptions
                    .iter_mut()
                    .find(|description| description.handle == grant.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !description.owners.contains(&owner) {
                    if description.owners.len() >= OWNER_LIMIT {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    description
                        .owners
                        .try_reserve(1)
                        .map_err(|_| IoError(IO_REGISTER_FAILED))?;
                    description.owners.push(owner);
                }
                self.grants.complete(token)?;
                return Ok(Some(Reply::Value(grant.handle.0)));
            }
            _ => {}
        }
        let index = self.index(owner, request.handle)?;
        match request.operation {
            Operation::InheritPrepare => self
                .grants
                .prepare(request.handle, owner, self.descriptions[index].flags)
                .map(Some),
            Operation::Close => {
                self.descriptions[index]
                    .owners
                    .retain(|candidate| *candidate != owner);
                self.grants.close(request.handle, owner);
                self.collect();
                Ok(Some(Reply::Value(0)))
            }
            operation => {
                let description = &mut self.descriptions[index];
                let value = match operation {
                    Operation::Read(destination) => {
                        if description.flags & IO_OPEN_READ == 0 {
                            return Err(IoError(IO_PERMISSION_DENIED));
                        }
                        let count = self
                            .volume
                            .read(&description.path, description.offset, destination)
                            .map_err(io_error)?;
                        description.offset += count as u64;
                        count as i64
                    }
                    Operation::Write(source) => {
                        if description.flags & IO_OPEN_WRITE == 0 {
                            return Err(IoError(IO_PERMISSION_DENIED));
                        }
                        let offset = if request.transfer_flags & IO_TRANSFER_APPEND != 0 {
                            u64::from(self.volume.entry(&description.path).map_err(io_error)?.size)
                        } else {
                            description.offset
                        };
                        let count = self
                            .volume
                            .write(&description.path, offset, source)
                            .map_err(io_error)?;
                        if count != 0 {
                            description.offset = offset + count as u64;
                        }
                        count as i64
                    }
                    Operation::Seek { offset, whence } => {
                        let base = match whence {
                            IO_SEEK_SET => 0,
                            IO_SEEK_CURRENT => description.offset,
                            IO_SEEK_END => u64::from(
                                self.volume.entry(&description.path).map_err(io_error)?.size,
                            ),
                            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                        };
                        let position = base
                            .checked_add_signed(offset)
                            .filter(|position| *position <= u64::from(u32::MAX))
                            .ok_or(IoError(IO_OPERATION_NOT_SUPPORTED))?;
                        description.offset = position;
                        position as i64
                    }
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                Ok(Some(Reply::Value(value)))
            }
        }
    }
}

impl<D: Device + 'static> IoComponent for Fat32<D> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        self.execute(request, hunde::runtime::credentials)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.release(session);
        Ok(())
    }

    fn dispose(&mut self) -> Result<(), IoError> {
        if !self.descriptions.is_empty() || !self.grants.is_empty() {
            return Err(IoError(IO_WOULD_BLOCK));
        }
        Ok(())
    }
}

impl hunde::component::FilesystemComponent for Fat32<hunde::block_host::HostedDevice> {
    fn mount(device: hunde::block_host::HostedDevice) -> Result<Self, IoError> {
        Self::mount(device).map_err(io_error)
    }
}

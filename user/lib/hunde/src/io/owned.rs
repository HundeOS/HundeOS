use super::{FileWatch, Handle, InheritanceToken, Operation, ProcessId, Reply, Request, SessionId};
use crate::abi::IoError;
use alloc::vec::Vec;

enum OwnedOperation {
    GetTerminalAttributes(crate::terminal::Attributes),
    SetTerminalAttributes(crate::terminal::Attributes),
    SetForegroundProcess(ProcessId),
    Open(Vec<u8>, u64),
    Accept,
    Read(Vec<u8>),
    Write(Vec<u8>),
    Close,
    Mount(Vec<u8>),
    Release,
    ListDirectory(Vec<u8>, u64),
    FileInfo(Vec<u8>),
    WatchFile,
    WaitFileChange(Vec<FileWatch>),
    Mkdir(Vec<u8>),
    Remove(Vec<u8>, u64),
    Link(Vec<u8>, Vec<u8>),
    Seek(i64, u64),
    PipeCreate(bool, u64),
    InheritPrepare,
    InheritCommit(InheritanceToken, ProcessId),
    InheritClaim(InheritanceToken, u64),
    InheritAbort(InheritanceToken),
}

pub struct OwnedRequest {
    session: SessionId,
    pid: ProcessId,
    handle: Handle,
    transfer_flags: u64,
    operation: OwnedOperation,
}

impl OwnedRequest {
    pub fn new(request: Request<'_>) -> Self {
        let operation = match request.operation {
            Operation::GetTerminalAttributes(attributes) => {
                OwnedOperation::GetTerminalAttributes(*attributes)
            }
            Operation::SetTerminalAttributes(attributes) => {
                OwnedOperation::SetTerminalAttributes(attributes)
            }
            Operation::SetForegroundProcess(pid) => OwnedOperation::SetForegroundProcess(pid),
            Operation::Open(path, flags) => OwnedOperation::Open(path.into(), flags),
            Operation::Accept => OwnedOperation::Accept,
            Operation::Read(buffer) => OwnedOperation::Read(alloc::vec![0; buffer.len()]),
            Operation::Write(buffer) => OwnedOperation::Write(buffer.into()),
            Operation::Close => OwnedOperation::Close,
            Operation::Mount(config) => OwnedOperation::Mount(config.into()),
            Operation::Release => OwnedOperation::Release,
            Operation::ListDirectory(path, offset) => {
                OwnedOperation::ListDirectory(path.into(), offset)
            }
            Operation::FileInfo(path) => OwnedOperation::FileInfo(path.into()),
            Operation::WatchFile => OwnedOperation::WatchFile,
            Operation::WaitFileChange(watches) => OwnedOperation::WaitFileChange(watches),
            Operation::Mkdir(path) => OwnedOperation::Mkdir(path.into()),
            Operation::Remove(path, flags) => OwnedOperation::Remove(path.into(), flags),
            Operation::Link { source, target } => {
                OwnedOperation::Link(source.into(), target.into())
            }
            Operation::Seek { offset, whence } => OwnedOperation::Seek(offset, whence),
            Operation::PipeCreate { buffered, flags } => {
                OwnedOperation::PipeCreate(buffered, flags)
            }
            Operation::InheritPrepare => OwnedOperation::InheritPrepare,
            Operation::InheritCommit { token, target } => {
                OwnedOperation::InheritCommit(token, target)
            }
            Operation::InheritClaim { token, flags } => OwnedOperation::InheritClaim(token, flags),
            Operation::InheritAbort(token) => OwnedOperation::InheritAbort(token),
        };
        Self {
            session: request.session,
            pid: request.pid,
            handle: request.handle,
            transfer_flags: request.transfer_flags,
            operation,
        }
    }

    pub fn with_request<Value>(&mut self, callback: impl FnOnce(Request<'_>) -> Value) -> Value {
        let operation = match &mut self.operation {
            OwnedOperation::GetTerminalAttributes(attributes) => {
                Operation::GetTerminalAttributes(attributes)
            }
            OwnedOperation::SetTerminalAttributes(attributes) => {
                Operation::SetTerminalAttributes(*attributes)
            }
            OwnedOperation::SetForegroundProcess(pid) => Operation::SetForegroundProcess(*pid),
            OwnedOperation::Open(path, flags) => Operation::Open(path, *flags),
            OwnedOperation::Accept => Operation::Accept,
            OwnedOperation::Read(buffer) => Operation::Read(buffer),
            OwnedOperation::Write(buffer) => Operation::Write(buffer),
            OwnedOperation::Close => Operation::Close,
            OwnedOperation::Mount(config) => Operation::Mount(config),
            OwnedOperation::Release => Operation::Release,
            OwnedOperation::ListDirectory(path, offset) => Operation::ListDirectory(path, *offset),
            OwnedOperation::FileInfo(path) => Operation::FileInfo(path),
            OwnedOperation::WatchFile => Operation::WatchFile,
            OwnedOperation::WaitFileChange(watches) => Operation::WaitFileChange(watches.clone()),
            OwnedOperation::Mkdir(path) => Operation::Mkdir(path),
            OwnedOperation::Remove(path, flags) => Operation::Remove(path, *flags),
            OwnedOperation::Link(source, target) => Operation::Link { source, target },
            OwnedOperation::Seek(offset, whence) => Operation::Seek {
                offset: *offset,
                whence: *whence,
            },
            OwnedOperation::PipeCreate(buffered, flags) => Operation::PipeCreate {
                buffered: *buffered,
                flags: *flags,
            },
            OwnedOperation::InheritPrepare => Operation::InheritPrepare,
            OwnedOperation::InheritCommit(token, target) => Operation::InheritCommit {
                token: *token,
                target: *target,
            },
            OwnedOperation::InheritClaim(token, flags) => Operation::InheritClaim {
                token: *token,
                flags: *flags,
            },
            OwnedOperation::InheritAbort(token) => Operation::InheritAbort(*token),
        };
        callback(
            Request::new(self.session, self.pid, self.handle, operation)
                .with_transfer_flags(self.transfer_flags),
        )
    }

    pub fn complete(self, request: Request<'_>, reply: Reply) -> Result<Reply, IoError> {
        if let Operation::GetTerminalAttributes(target) = request.operation {
            let OwnedOperation::GetTerminalAttributes(attributes) = self.operation else {
                return Err(IoError(crate::protocol::IO_PANIC));
            };
            *target = attributes;
            return Ok(reply);
        }
        if let Operation::Read(target) = request.operation {
            let OwnedOperation::Read(source) = self.operation else {
                return Err(IoError(crate::protocol::IO_PANIC));
            };
            let Reply::Value(count) = reply else {
                return Err(IoError(crate::protocol::IO_PANIC));
            };
            let count = usize::try_from(count).map_err(|_| IoError(crate::protocol::IO_PANIC))?;
            let source = source
                .get(..count)
                .ok_or(IoError(crate::protocol::IO_PANIC))?;
            let target = target
                .get_mut(..count)
                .ok_or(IoError(crate::protocol::IO_PANIC))?;
            target.copy_from_slice(source);
        }
        Ok(reply)
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    fn request(operation: Operation<'_>) -> Request<'_> {
        Request::new(SessionId(3), ProcessId(7), Handle(9), operation).with_transfer_flags(2)
    }
    #[test]
    fn owned_read_does_not_borrow_client_memory_and_only_copies_completed_bytes() {
        let mut target = [99; 4];
        let mut owned = OwnedRequest::new(request(Operation::Read(&mut target)));
        owned.with_request(|request| {
            assert_eq!(
                (
                    request.session,
                    request.pid,
                    request.handle,
                    request.transfer_flags
                ),
                (SessionId(3), ProcessId(7), Handle(9), 2)
            );
            if let Operation::Read(buffer) = request.operation {
                buffer[..2].copy_from_slice(b"ok");
            }
        });
        assert_eq!(target, [99; 4]);
        owned
            .complete(request(Operation::Read(&mut target)), Reply::Value(2))
            .unwrap();
        assert_eq!(target, [b'o', b'k', 99, 99]);
    }
    #[test]
    fn owned_writes_and_paths_survive_their_callers_and_invalid_read_counts_fail() {
        let mut source = alloc::vec![1, 2, 3];
        let mut owned = OwnedRequest::new(request(Operation::Write(&source)));
        source.fill(9);
        owned.with_request(|request| {
            assert!(matches!(request.operation, Operation::Write([1, 2, 3])));
        });
        let mut target = [0; 2];
        let owned = OwnedRequest::new(request(Operation::Read(&mut target)));
        assert!(
            owned
                .complete(request(Operation::Read(&mut target)), Reply::Value(3))
                .is_err()
        );
    }
}

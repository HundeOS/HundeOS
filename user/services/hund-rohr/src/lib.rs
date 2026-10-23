#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::collections::VecDeque;
use alloc::vec;
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{Handle, Operation, READ, Reply, Request, SessionId, WRITE};
use hunde::protocol::{
    IO_BROKEN_PIPE, IO_EOF, IO_OPERATION_NOT_SUPPORTED, IO_PERMISSION_DENIED, IO_REGISTER_FAILED,
    IO_TRANSFER_APPEND, IO_TRANSFER_NONBLOCK, IO_UNKNOWN_HANDLE, IO_WOULD_BLOCK,
};

use hunde::inheritance::{Grants, Owner};

const PIPE_CAPACITY: usize = 65536;
const ATOMIC_WRITE: usize = 4096;
const RESOURCE_LIMIT: usize = 64;

struct Endpoint {
    handle: Handle,
    owners: Vec<Owner>,
}

impl Endpoint {
    fn new(handle: Handle, owner: Owner) -> Self {
        Self {
            handle,
            owners: vec![owner],
        }
    }

    fn live(&self, grants: &Grants) -> bool {
        !self.owners.is_empty() || grants.retains(self.handle)
    }
}

struct Pipe {
    reader: Endpoint,
    writer: Endpoint,
    bytes: VecDeque<u8>,
}

impl Pipe {
    fn new(reader: Handle, writer: Handle, owner: Owner) -> Result<Self, IoError> {
        let mut bytes = VecDeque::new();
        bytes
            .try_reserve_exact(PIPE_CAPACITY)
            .map_err(|_| IoError(IO_REGISTER_FAILED))?;
        Ok(Self {
            reader: Endpoint::new(reader, owner),
            writer: Endpoint::new(writer, owner),
            bytes,
        })
    }

    fn endpoint(&self, handle: Handle) -> Option<&Endpoint> {
        if self.reader.handle == handle {
            Some(&self.reader)
        } else if self.writer.handle == handle {
            Some(&self.writer)
        } else {
            None
        }
    }

    fn endpoint_mut(&mut self, handle: Handle) -> Option<&mut Endpoint> {
        if self.reader.handle == handle {
            Some(&mut self.reader)
        } else if self.writer.handle == handle {
            Some(&mut self.writer)
        } else {
            None
        }
    }

    fn flags(&self, handle: Handle) -> u64 {
        if self.reader.handle == handle {
            READ
        } else {
            WRITE
        }
    }
}

pub struct PipeService {
    pipes: Vec<Pipe>,
    grants: Grants,
    next_handle: i64,
}

impl Default for PipeService {
    fn default() -> Self {
        Self {
            pipes: Vec::new(),
            grants: Grants::default(),
            next_handle: 1 << 32,
        }
    }
}

impl PipeService {
    fn collect(&mut self) {
        self.pipes
            .retain(|pipe| pipe.reader.live(&self.grants) || pipe.writer.live(&self.grants));
    }

    fn create(
        &mut self,
        owner: Owner,
        buffered: bool,
        flags: u64,
    ) -> Result<Option<Reply>, IoError> {
        if !buffered || flags & (READ | WRITE) != (READ | WRITE) {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if self.pipes.len() == RESOURCE_LIMIT || self.next_handle > i64::MAX - 2 {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let reader = Handle(self.next_handle);
        let writer = Handle(self.next_handle + 1);
        let pipe = Pipe::new(reader, writer, owner)?;
        self.pipes.push(pipe);
        self.next_handle += 2;
        Ok(Some(Reply::Pipe { reader, writer }))
    }

    fn inherit(
        &mut self,
        owner: Owner,
        operation: Operation<'_>,
    ) -> Result<Option<Reply>, IoError> {
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
                let endpoint = self
                    .pipes
                    .iter_mut()
                    .find_map(|pipe| pipe.endpoint_mut(grant.handle))
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                if !endpoint.owners.contains(&owner) {
                    if endpoint.owners.len() == RESOURCE_LIMIT {
                        return Err(IoError(IO_REGISTER_FAILED));
                    }
                    endpoint.owners.push(owner);
                }
                self.grants.complete(token)?;
                return Ok(Some(Reply::Value(grant.handle.0)));
            }
            _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
        Ok(Some(Reply::Value(0)))
    }

    pub fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        match request.operation {
            Operation::PipeCreate { buffered, flags } => {
                return self.create(owner, buffered, flags);
            }
            Operation::InheritCommit { .. }
            | Operation::InheritClaim { .. }
            | Operation::InheritAbort(_) => return self.inherit(owner, request.operation),
            Operation::Release => {
                self.disconnect(request.session)?;
                return Ok(Some(Reply::Value(0)));
            }
            _ => {}
        }
        let index = self
            .pipes
            .iter()
            .position(|pipe| {
                pipe.endpoint(request.handle)
                    .is_some_and(|endpoint| endpoint.owners.contains(&owner))
            })
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        let flags = self.pipes[index].flags(request.handle);
        if matches!(request.operation, Operation::InheritPrepare) {
            return self.grants.prepare(request.handle, owner, flags).map(Some);
        }
        let pipe = &mut self.pipes[index];
        match request.operation {
            Operation::Close => {
                pipe.endpoint_mut(request.handle)
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?
                    .owners
                    .retain(|candidate| *candidate != owner);
                self.grants.close(request.handle, owner);
                self.collect();
                Ok(Some(Reply::Value(0)))
            }
            Operation::Read(target) => {
                if flags != READ {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if request.transfer_flags & !IO_TRANSFER_NONBLOCK != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if target.is_empty() {
                    return Ok(Some(Reply::Value(0)));
                }
                if pipe.bytes.is_empty() {
                    if !pipe.writer.live(&self.grants) {
                        return Err(IoError(IO_EOF));
                    }
                    return if request.transfer_flags & IO_TRANSFER_NONBLOCK != 0 {
                        Err(IoError(IO_WOULD_BLOCK))
                    } else {
                        Ok(None)
                    };
                }
                let count = target.len().min(pipe.bytes.len());
                for (destination, value) in target.iter_mut().zip(pipe.bytes.drain(..count)) {
                    *destination = value;
                }
                Ok(Some(Reply::Value(count as i64)))
            }
            Operation::Write(source) => {
                if flags != WRITE {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if request.transfer_flags & !(IO_TRANSFER_NONBLOCK | IO_TRANSFER_APPEND) != 0 {
                    return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
                }
                if source.is_empty() {
                    return Ok(Some(Reply::Value(0)));
                }
                if !pipe.reader.live(&self.grants) {
                    return Err(IoError(IO_BROKEN_PIPE));
                }
                let available = PIPE_CAPACITY - pipe.bytes.len();
                if available == 0 || (source.len() <= ATOMIC_WRITE && available < source.len()) {
                    return if request.transfer_flags & IO_TRANSFER_NONBLOCK != 0 {
                        Err(IoError(IO_WOULD_BLOCK))
                    } else {
                        Ok(None)
                    };
                }
                let count = available.min(source.len());
                pipe.bytes.extend(&source[..count]);
                Ok(Some(Reply::Value(count as i64)))
            }
            _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
        }
    }
}

impl IoComponent for PipeService {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        PipeService::execute(self, request)
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.grants.release(session);
        for pipe in &mut self.pipes {
            pipe.reader.owners.retain(|owner| owner.session != session);
            pipe.writer.owners.retain(|owner| owner.session != session);
        }
        self.collect();
        Ok(())
    }
}

#[cfg(feature = "component")]
hunde::io_component!(PipeService, b"Rust pipe component panicked");

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::{InheritanceToken, ProcessId};

    fn call(
        service: &mut PipeService,
        session: u64,
        pid: u64,
        handle: Handle,
        operation: Operation<'_>,
    ) -> Result<Option<Reply>, IoError> {
        service.execute(Request::new(
            SessionId(session),
            ProcessId(pid),
            handle,
            operation,
        ))
    }

    fn pair(service: &mut PipeService) -> (Handle, Handle) {
        match call(
            service,
            1,
            10,
            Handle(0),
            Operation::PipeCreate {
                buffered: true,
                flags: READ | WRITE,
            },
        )
        .unwrap()
        {
            Some(Reply::Pipe { reader, writer }) => (reader, writer),
            _ => panic!("Expected paired endpoints"),
        }
    }

    fn prepare(service: &mut PipeService, handle: Handle) -> (InheritanceToken, u64) {
        match call(service, 1, 10, handle, Operation::InheritPrepare).unwrap() {
            Some(Reply::Inheritance { token, flags }) => (token, flags),
            _ => panic!("Expected inheritance grant"),
        }
    }

    #[test]
    fn binary_reads_pending_eof_and_broken_pipe() {
        let mut service = PipeService::default();
        let (reader, writer) = pair(&mut service);
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0; 8])),
            Ok(None)
        );
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Write(&[0, 255, 42])),
            Ok(Some(Reply::Value(3)))
        );
        call(&mut service, 1, 10, writer, Operation::Close).unwrap();
        let mut target = [0; 8];
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut target)),
            Ok(Some(Reply::Value(3)))
        );
        assert_eq!(&target[..3], &[0, 255, 42]);
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut target)),
            Err(IoError(IO_EOF))
        );
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [])),
            Ok(Some(Reply::Value(0)))
        );
        call(&mut service, 1, 10, reader, Operation::Close).unwrap();
        assert!(service.pipes.is_empty());
        let (reader, writer) = pair(&mut service);
        call(&mut service, 1, 10, reader, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Write(b"x")),
            Err(IoError(IO_BROKEN_PIPE))
        );
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Write(b"")),
            Ok(Some(Reply::Value(0)))
        );
        service.disconnect(SessionId(1)).unwrap();
        assert!(service.pipes.is_empty());
    }

    #[test]
    fn backpressure_atomicity_nonblocking_and_wraparound() {
        let mut service = PipeService::default();
        let (reader, writer) = pair(&mut service);
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                writer,
                Operation::Write(&vec![7; PIPE_CAPACITY + 1])
            ),
            Ok(Some(Reply::Value(PIPE_CAPACITY as i64)))
        );
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Write(b"x")),
            Ok(None)
        );
        let request = Request::new(SessionId(1), ProcessId(10), writer, Operation::Write(b"x"))
            .with_transfer_flags(IO_TRANSFER_NONBLOCK);
        assert_eq!(service.execute(request), Err(IoError(IO_WOULD_BLOCK)));
        let mut target = [0; ATOMIC_WRITE - 1];
        call(&mut service, 1, 10, reader, Operation::Read(&mut target)).unwrap();
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                writer,
                Operation::Write(&[9; ATOMIC_WRITE])
            ),
            Ok(None)
        );
        call(&mut service, 1, 10, reader, Operation::Read(&mut [0])).unwrap();
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                writer,
                Operation::Write(&[9; ATOMIC_WRITE])
            ),
            Ok(Some(Reply::Value(ATOMIC_WRITE as i64)))
        );
        let mut all = vec![0; PIPE_CAPACITY];
        call(&mut service, 1, 10, reader, Operation::Read(&mut all)).unwrap();
        assert!(
            all[..PIPE_CAPACITY - ATOMIC_WRITE]
                .iter()
                .all(|value| *value == 7)
        );
        assert!(
            all[PIPE_CAPACITY - ATOMIC_WRITE..]
                .iter()
                .all(|value| *value == 9)
        );
        let request = Request::new(
            SessionId(1),
            ProcessId(10),
            reader,
            Operation::Read(&mut all),
        )
        .with_transfer_flags(IO_TRANSFER_NONBLOCK);
        assert_eq!(service.execute(request), Err(IoError(IO_WOULD_BLOCK)));
    }

    #[test]
    fn identity_permissions_flags_limits_and_stale_handles() {
        let mut service = PipeService::default();
        assert_eq!(
            call(
                &mut service,
                0,
                10,
                Handle(0),
                Operation::PipeCreate {
                    buffered: true,
                    flags: READ | WRITE
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                Handle(0),
                Operation::PipeCreate {
                    buffered: false,
                    flags: READ | WRITE
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                Handle(0),
                Operation::PipeCreate {
                    buffered: true,
                    flags: READ
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        let (reader, writer) = pair(&mut service);
        for (session, pid) in [(2, 10), (1, 20)] {
            assert_eq!(
                call(&mut service, session, pid, reader, Operation::Close),
                Err(IoError(IO_UNKNOWN_HANDLE))
            );
        }
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Read(&mut [0])),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Write(b"x")),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        let mut target = [0];
        let request = Request::new(
            SessionId(1),
            ProcessId(10),
            reader,
            Operation::Read(&mut target),
        )
        .with_transfer_flags(IO_TRANSFER_APPEND);
        assert_eq!(
            service.execute(request),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        call(&mut service, 1, 10, reader, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Close),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        for _ in 1..RESOURCE_LIMIT {
            pair(&mut service);
        }
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                Handle(0),
                Operation::PipeCreate {
                    buffered: true,
                    flags: READ | WRITE
                }
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        service.disconnect(SessionId(1)).unwrap();
        assert!(service.pipes.is_empty());
        service.next_handle = i64::MAX - 1;
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                Handle(0),
                Operation::PipeCreate {
                    buffered: true,
                    flags: READ | WRITE
                }
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
    }

    #[test]
    fn committed_grants_preserve_endpoints_until_authenticated_single_use_claim() {
        let mut service = PipeService::default();
        let (reader, writer) = pair(&mut service);
        let (token, flags) = prepare(&mut service, writer);
        assert_eq!(flags, WRITE);
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritCommit {
                    token,
                    target: ProcessId(20)
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        call(
            &mut service,
            1,
            10,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(20),
            },
        )
        .unwrap();
        call(&mut service, 1, 10, writer, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0])),
            Ok(None)
        );
        assert_eq!(
            call(
                &mut service,
                2,
                21,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritClaim {
                    token,
                    flags: READ | WRITE
                }
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Ok(Some(Reply::Value(writer.0)))
        );
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            call(&mut service, 2, 20, writer, Operation::Write(b"x")),
            Ok(Some(Reply::Value(1)))
        );
        call(&mut service, 1, 10, reader, Operation::Read(&mut [0])).unwrap();
        service.disconnect(SessionId(2)).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0])),
            Err(IoError(IO_EOF))
        );
    }

    #[test]
    fn disconnect_and_abort_release_grants_without_losing_committed_children() {
        let mut service = PipeService::default();
        let (reader, writer) = pair(&mut service);
        let (unused, _) = prepare(&mut service, reader);
        let (token, flags) = prepare(&mut service, writer);
        call(
            &mut service,
            1,
            10,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(20),
            },
        )
        .unwrap();
        service.disconnect(SessionId(1)).unwrap();
        assert_eq!(service.grants.len(), 1);
        assert_eq!(service.pipes.len(), 1);
        assert_eq!(
            call(
                &mut service,
                1,
                10,
                Handle(0),
                Operation::InheritAbort(unused)
            ),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritClaim { token, flags }
            ),
            Ok(Some(Reply::Value(writer.0)))
        );
        assert_eq!(
            call(&mut service, 2, 20, writer, Operation::Write(b"x")),
            Err(IoError(IO_BROKEN_PIPE))
        );
        service.disconnect(SessionId(2)).unwrap();
        assert!(service.pipes.is_empty());
        let (reader, _) = pair(&mut service);
        let (token, _) = prepare(&mut service, reader);
        assert_eq!(
            call(
                &mut service,
                2,
                20,
                Handle(0),
                Operation::InheritAbort(token)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        call(
            &mut service,
            1,
            10,
            Handle(0),
            Operation::InheritAbort(token),
        )
        .unwrap();
        assert!(service.grants.is_empty());
    }

    #[test]
    fn inherited_owners_close_independently_and_only_last_writer_causes_eof() {
        let mut service = PipeService::default();
        let (reader, writer) = pair(&mut service);
        let (token, flags) = prepare(&mut service, writer);
        call(
            &mut service,
            1,
            10,
            Handle(0),
            Operation::InheritCommit {
                token,
                target: ProcessId(20),
            },
        )
        .unwrap();
        call(
            &mut service,
            2,
            20,
            Handle(0),
            Operation::InheritClaim { token, flags },
        )
        .unwrap();
        call(&mut service, 1, 10, writer, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0])),
            Ok(None)
        );
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::Write(b"x")),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert_eq!(
            call(&mut service, 2, 20, writer, Operation::Write(b"x")),
            Ok(Some(Reply::Value(1)))
        );
        call(&mut service, 2, 20, writer, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0])),
            Ok(Some(Reply::Value(1)))
        );
        assert_eq!(
            call(&mut service, 1, 10, reader, Operation::Read(&mut [0])),
            Err(IoError(IO_EOF))
        );
    }

    #[test]
    fn grant_limits_and_uncommitted_close_cleanup_are_bounded() {
        let mut service = PipeService::default();
        let (_, writer) = pair(&mut service);
        for _ in 0..RESOURCE_LIMIT {
            prepare(&mut service, writer);
        }
        assert_eq!(
            call(&mut service, 1, 10, writer, Operation::InheritPrepare),
            Err(IoError(IO_REGISTER_FAILED))
        );
        call(&mut service, 1, 10, writer, Operation::Close).unwrap();
        assert!(service.grants.is_empty());
    }
}

#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::collections::VecDeque;
use alloc::vec::Vec;
use hunde::abi::IoError;
use hunde::component::IoComponent;
use hunde::io::{BIND, Handle, LISTEN, Operation, ProcessId, READ, Request, SessionId, WRITE};
use hunde::protocol::{
    IO_INVALID_PATH, IO_NO_DEVICE_FOUND, IO_NOT_OPEN, IO_OPERATION_NOT_IMPLEMENTED,
    IO_OPERATION_NOT_SUPPORTED, IO_PERMISSION_DENIED, IO_REGISTER_FAILED, IO_UNKNOWN_HANDLE,
};

const RESOURCE_LIMIT: usize = 64;
const BACKLOG: usize = 16;
const BUFFER_SIZE: usize = 1024;

enum ResourceKind {
    Listener {
        name: Vec<u8>,
        pending: VecDeque<Handle>,
    },
    Connection {
        peer: Option<Handle>,
        bytes: VecDeque<u8>,
        flags: u64,
    },
}

struct Resource {
    handle: Handle,
    session: SessionId,
    pid: ProcessId,
    kind: ResourceKind,
}

pub struct SocketService {
    resources: Vec<Resource>,
    next_handle: i64,
}

impl Default for SocketService {
    fn default() -> Self {
        Self {
            resources: Vec::new(),
            next_handle: 1 << 32,
        }
    }
}

impl SocketService {
    fn allocate_handle(&mut self) -> Result<Handle, IoError> {
        let next = self
            .next_handle
            .checked_add(1)
            .ok_or(IoError(IO_REGISTER_FAILED))?;
        let handle = Handle(self.next_handle);
        self.next_handle = next;
        Ok(handle)
    }

    fn index(&self, handle: Handle) -> Result<usize, IoError> {
        self.resources
            .iter()
            .position(|resource| resource.handle == handle)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))
    }

    fn open(
        &mut self,
        session: SessionId,
        pid: ProcessId,
        path: &[u8],
        flags: u64,
    ) -> Result<Option<i64>, IoError> {
        if !path.starts_with(b"socket://") || path.len() <= 9 || path.len() > 256 {
            return Err(IoError(IO_INVALID_PATH));
        }
        if flags & !(READ | WRITE | BIND | LISTEN) != 0 {
            return Err(IoError(IO_INVALID_PATH));
        }
        let listener = self.resources.iter().position(|resource| {
            matches!(&resource.kind,
            ResourceKind::Listener { name, .. } if name == path)
        });
        if flags & (BIND | LISTEN) != 0 {
            if listener.is_some() || self.resources.len() == RESOURCE_LIMIT {
                return Err(IoError(IO_REGISTER_FAILED));
            }
            let handle = self.allocate_handle()?;
            self.resources.push(Resource {
                handle,
                session,
                pid,
                kind: ResourceKind::Listener {
                    name: path.to_vec(),
                    pending: VecDeque::new(),
                },
            });
            return Ok(Some(handle.0));
        }
        let listener = listener.ok_or(IoError(IO_NO_DEVICE_FOUND))?;
        if self.resources.len() + 2 > RESOURCE_LIMIT || self.next_handle > i64::MAX - 2 {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let (server_session, server_pid) = {
            let resource = &self.resources[listener];
            match &resource.kind {
                ResourceKind::Listener { pending, .. } if pending.len() < BACKLOG => {
                    (resource.session, resource.pid)
                }
                _ => return Err(IoError(IO_REGISTER_FAILED)),
            }
        };
        let client = self.allocate_handle()?;
        let server = self.allocate_handle()?;
        if let ResourceKind::Listener { pending, .. } = &mut self.resources[listener].kind {
            pending.push_back(server);
        }
        self.resources.push(Resource {
            handle: client,
            session,
            pid,
            kind: ResourceKind::Connection {
                peer: Some(server),
                bytes: VecDeque::new(),
                flags,
            },
        });
        self.resources.push(Resource {
            handle: server,
            session: server_session,
            pid: server_pid,
            kind: ResourceKind::Connection {
                peer: Some(client),
                bytes: VecDeque::new(),
                flags: READ | WRITE,
            },
        });
        Ok(Some(client.0))
    }

    fn close(&mut self, handle: Handle) {
        let Ok(index) = self.index(handle) else {
            return;
        };
        let removed = self.resources.remove(index);
        if let ResourceKind::Listener { pending, .. } = removed.kind {
            for endpoint in pending {
                self.close(endpoint);
            }
        }
        for resource in &mut self.resources {
            match &mut resource.kind {
                ResourceKind::Listener { pending, .. } => {
                    pending.retain(|candidate| *candidate != handle)
                }
                ResourceKind::Connection { peer, .. } if *peer == Some(handle) => *peer = None,
                _ => {}
            }
        }
    }

    pub fn disconnect(&mut self, session: SessionId) {
        while let Some(handle) = self
            .resources
            .iter()
            .find(|resource| resource.session == session)
            .map(|resource| resource.handle)
        {
            self.close(handle);
        }
    }

    pub fn execute(&mut self, request: Request<'_>) -> Result<Option<i64>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        if let Operation::Open(path, flags) = request.operation {
            return self.open(request.session, request.pid, path, flags);
        }
        if matches!(request.operation, Operation::Release) {
            self.disconnect(request.session);
            return Ok(Some(0));
        }
        let index = self.index(request.handle)?;
        if self.resources[index].session != request.session
            || self.resources[index].pid != request.pid
        {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        match request.operation {
            Operation::Close => {
                self.close(request.handle);
                Ok(Some(0))
            }
            Operation::Accept => match &mut self.resources[index].kind {
                ResourceKind::Listener { pending, .. } => {
                    Ok(pending.pop_front().map(|handle| handle.0))
                }
                _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
            },
            Operation::Read(target) => match &mut self.resources[index].kind {
                ResourceKind::Connection { flags, .. } if *flags & READ == 0 => {
                    Err(IoError(IO_PERMISSION_DENIED))
                }
                ResourceKind::Connection { bytes, peer, .. } => {
                    if target.is_empty() {
                        return Ok(Some(0));
                    }
                    if bytes.is_empty() {
                        return Ok(if peer.is_some() { None } else { Some(0) });
                    }
                    let count = target.len().min(bytes.len());
                    for (destination, value) in target.iter_mut().zip(bytes.drain(..count)) {
                        *destination = value;
                    }
                    Ok(Some(count as i64))
                }
                _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
            },
            Operation::Write(source) => {
                let peer = match &self.resources[index].kind {
                    ResourceKind::Connection { flags, .. } if *flags & WRITE == 0 => {
                        return Err(IoError(IO_PERMISSION_DENIED));
                    }
                    ResourceKind::Connection { peer, .. } => *peer,
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                };
                if source.is_empty() {
                    return Ok(Some(0));
                }
                let peer_index = self.index(peer.ok_or(IoError(IO_NOT_OPEN))?)?;
                match &mut self.resources[peer_index].kind {
                    ResourceKind::Connection { bytes, .. } => {
                        let count = source.len().min(BUFFER_SIZE - bytes.len());
                        if count == 0 {
                            return Ok(None);
                        }
                        bytes.extend(&source[..count]);
                        Ok(Some(count as i64))
                    }
                    _ => Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                }
            }
            _ => Err(IoError(IO_OPERATION_NOT_IMPLEMENTED)),
        }
    }
}

impl IoComponent for SocketService {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<hunde::io::Reply>, IoError> {
        SocketService::execute(self, request).map(|value| value.map(hunde::io::Reply::Value))
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        SocketService::disconnect(self, session);
        Ok(())
    }
}

#[cfg(feature = "component")]
hunde::io_component!(SocketService, b"Rust socket component panicked");

#[cfg(test)]
mod tests {
    use super::*;

    fn call(
        service: &mut SocketService,
        session: u64,
        handle: i64,
        operation: Operation<'_>,
    ) -> Result<Option<i64>, IoError> {
        service.execute(Request::new(
            SessionId(session),
            ProcessId(session),
            Handle(handle),
            operation,
        ))
    }

    fn pair(service: &mut SocketService) -> (i64, i64, i64) {
        let listener = call(service, 1, 0, Operation::Open(b"socket://test", BIND))
            .unwrap()
            .unwrap();
        let client = call(
            service,
            2,
            0,
            Operation::Open(b"socket://test", READ | WRITE),
        )
        .unwrap()
        .unwrap();
        let server = call(service, 1, listener, Operation::Accept)
            .unwrap()
            .unwrap();
        (listener, client, server)
    }

    #[test]
    fn binary_data_pending_and_eof() {
        let mut service = SocketService::default();
        let (_, client, server) = pair(&mut service);
        let mut target = [0; 4];
        assert_eq!(
            call(&mut service, 1, server, Operation::Read(&mut target)),
            Ok(None)
        );
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(&[0, 255, 2])),
            Ok(Some(3))
        );
        assert_eq!(call(&mut service, 2, client, Operation::Close), Ok(Some(0)));
        assert_eq!(
            call(&mut service, 1, server, Operation::Read(&mut target)),
            Ok(Some(3))
        );
        assert_eq!(&target[..3], &[0, 255, 2]);
        assert_eq!(
            call(&mut service, 1, server, Operation::Read(&mut target)),
            Ok(Some(0))
        );
    }

    #[test]
    fn backpressure_and_partial_writes() {
        let mut service = SocketService::default();
        let (_, client, server) = pair(&mut service);
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(&[42; 1100])),
            Ok(Some(1024))
        );
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(&[1])),
            Ok(None)
        );
        assert_eq!(
            call(&mut service, 1, server, Operation::Read(&mut [0; 7])),
            Ok(Some(7))
        );
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(&[1; 10])),
            Ok(Some(7))
        );
    }

    #[test]
    fn identity_ownership_stale_handles_and_operation_kind() {
        let mut service = SocketService::default();
        let (listener, client, server) = pair(&mut service);
        assert_eq!(
            call(&mut service, 0, client, Operation::Close),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 1, client, Operation::Close),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 1, server, Operation::Accept),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        assert_eq!(
            call(&mut service, 1, listener, Operation::Read(&mut [0])),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
        call(&mut service, 2, client, Operation::Close).unwrap();
        assert_eq!(
            call(&mut service, 2, client, Operation::Close),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn listener_owns_name_and_accept_waits() {
        let mut service = SocketService::default();
        let mut name = *b"socket://test";
        let listener = call(&mut service, 1, 0, Operation::Open(&name, LISTEN))
            .unwrap()
            .unwrap();
        name.fill(0);
        assert_eq!(call(&mut service, 1, listener, Operation::Accept), Ok(None));
        assert!(
            call(&mut service, 2, 0, Operation::Open(b"socket://test", READ))
                .unwrap()
                .is_some()
        );
        assert!(
            call(&mut service, 1, listener, Operation::Accept)
                .unwrap()
                .is_some()
        );
    }

    #[test]
    fn disconnect_closes_pending_connections_and_releases_name() {
        let mut service = SocketService::default();
        let listener = call(&mut service, 1, 0, Operation::Open(b"socket://test", BIND))
            .unwrap()
            .unwrap();
        let client = call(
            &mut service,
            2,
            0,
            Operation::Open(b"socket://test", READ | WRITE),
        )
        .unwrap()
        .unwrap();
        service.disconnect(SessionId(1));
        assert_eq!(service.resources.len(), 1);
        assert_eq!(
            call(&mut service, 2, client, Operation::Read(&mut [0])),
            Ok(Some(0))
        );
        assert_eq!(
            call(&mut service, 1, listener, Operation::Accept),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        assert!(call(&mut service, 1, 0, Operation::Open(b"socket://test", BIND)).is_ok());
        service.disconnect(SessionId(2));
        service.disconnect(SessionId(1));
        assert!(service.resources.is_empty());
    }

    #[test]
    fn permissions_and_limits() {
        let mut service = SocketService::default();
        let listener = call(&mut service, 1, 0, Operation::Open(b"socket://test", BIND))
            .unwrap()
            .unwrap();
        let client = call(&mut service, 2, 0, Operation::Open(b"socket://test", READ))
            .unwrap()
            .unwrap();
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(b"a")),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 1, 0, Operation::Open(b"socket://test", BIND)),
            Err(IoError(IO_REGISTER_FAILED))
        );
        for _ in 1..BACKLOG {
            call(&mut service, 2, 0, Operation::Open(b"socket://test", READ)).unwrap();
        }
        assert_eq!(
            call(&mut service, 2, 0, Operation::Open(b"socket://test", READ)),
            Err(IoError(IO_REGISTER_FAILED))
        );
        assert!(
            call(&mut service, 1, listener, Operation::Accept)
                .unwrap()
                .is_some()
        );
    }
}

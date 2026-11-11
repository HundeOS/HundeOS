#![no_std]

extern crate alloc;

mod loopback;
pub mod packet;

use alloc::vec;
use alloc::vec::Vec;
use core::str::FromStr;
use hunde::abi::IoError;
use hunde::component::{IoComponent, Schedule};
use hunde::inheritance::Owner;
use hunde::io::{BIND, Handle, LISTEN, Operation, READ, Reply, Request, SessionId, WRITE};
use hunde::protocol::{
    IO_INVALID_PATH, IO_NO_DEVICE_FOUND, IO_NOT_OPEN, IO_OPERATION_NOT_SUPPORTED,
    IO_PERMISSION_DENIED, IO_REGISTER_FAILED, IO_UNKNOWN_HANDLE,
};
use loopback::LocalDevice;
use smoltcp::iface::{
    Config, Interface, PollIngressSingleResult, PollResult, SocketHandle, SocketSet,
};
use smoltcp::socket::tcp;
use smoltcp::time::{Duration, Instant};
use smoltcp::wire::{HardwareAddress, IpAddress, IpCidr, IpEndpoint, Ipv4Address, Ipv6Address};

const RESOURCE_LIMIT: usize = 64;
const SOCKET_LIMIT: usize = 128;
const BACKLOG: usize = 4;
const BUFFER_SIZE: usize = 4096;
const CLOSE_TIMEOUT_USEC: u64 = 30_000_000;

enum Kind {
    Listener {
        endpoint: IpEndpoint,
        sockets: Vec<SocketHandle>,
    },
    Connection {
        socket: SocketHandle,
        connecting: bool,
        flags: u64,
        endpoint: IpEndpoint,
    },
}

struct Resource {
    handle: Handle,
    owner: Owner,
    kind: Kind,
}

impl Resource {
    fn new(handle: Handle, owner: Owner, kind: Kind) -> Self {
        Self {
            handle,
            owner,
            kind,
        }
    }
}

struct Closing {
    socket: SocketHandle,
    deadline: u64,
}

impl Closing {
    fn new(socket: SocketHandle, deadline: u64) -> Self {
        Self { socket, deadline }
    }
}

pub struct NetworkService {
    interface: Interface,
    device: LocalDevice,
    sockets: SocketSet<'static>,
    resources: Vec<Resource>,
    closing: Vec<Closing>,
    next_handle: i64,
    next_port: u16,
    now_usec: u64,
}

impl Default for NetworkService {
    fn default() -> Self {
        let mut device = LocalDevice::default();
        let mut config = Config::new(HardwareAddress::Ip);
        config.random_seed = 1;
        let mut interface = Interface::new(config, &mut device, Instant::ZERO);
        interface.update_ip_addrs(|addresses| {
            addresses
                .push(IpCidr::new(Ipv4Address::LOCALHOST.into(), 8))
                .unwrap();
            addresses
                .push(IpCidr::new(Ipv6Address::LOCALHOST.into(), 128))
                .unwrap();
        });
        Self {
            interface,
            device,
            sockets: SocketSet::new(Vec::new()),
            resources: Vec::new(),
            closing: Vec::new(),
            next_handle: 1,
            next_port: 49152,
            now_usec: 0,
        }
    }
}

fn endpoint(path: &[u8]) -> Result<IpEndpoint, IoError> {
    let text = core::str::from_utf8(path).map_err(|_| IoError(IO_INVALID_PATH))?;
    let address = text
        .strip_prefix("tcp://")
        .ok_or(IoError(IO_INVALID_PATH))?;
    let endpoint = if let Some(port) = address.strip_prefix("localhost:") {
        IpEndpoint::new(
            Ipv4Address::LOCALHOST.into(),
            port.parse().map_err(|_| IoError(IO_INVALID_PATH))?,
        )
    } else {
        IpEndpoint::from_str(address).map_err(|_| IoError(IO_INVALID_PATH))?
    };
    if endpoint.port == 0
        || !matches!(
            endpoint.addr,
            IpAddress::Ipv4(Ipv4Address::LOCALHOST) | IpAddress::Ipv6(Ipv6Address::LOCALHOST)
        )
    {
        return Err(IoError(IO_INVALID_PATH));
    }
    Ok(endpoint)
}

impl NetworkService {
    fn instant(&self) -> Instant {
        Instant::from_micros(self.now_usec.min(i64::MAX as u64) as i64)
    }

    fn new_socket(&mut self) -> Result<SocketHandle, IoError> {
        if self.sockets.iter().count() == SOCKET_LIMIT {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let mut socket = tcp::Socket::new(
            tcp::SocketBuffer::new(vec![0; BUFFER_SIZE]),
            tcp::SocketBuffer::new(vec![0; BUFFER_SIZE]),
        );
        socket.set_timeout(Some(Duration::from_secs(30)));
        Ok(self.sockets.add(socket))
    }

    fn handle(&mut self) -> Result<Handle, IoError> {
        let value = self.next_handle;
        self.next_handle = value.checked_add(1).ok_or(IoError(IO_REGISTER_FAILED))?;
        Ok(Handle(value))
    }

    fn index(&self, handle: Handle, owner: Owner) -> Result<usize, IoError> {
        let index = self
            .resources
            .iter()
            .position(|resource| resource.handle == handle)
            .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
        if self.resources[index].owner != owner {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        Ok(index)
    }

    fn listen_socket(&mut self, endpoint: IpEndpoint) -> Result<SocketHandle, IoError> {
        let handle = self.new_socket()?;
        if self
            .sockets
            .get_mut::<tcp::Socket>(handle)
            .listen(endpoint)
            .is_err()
        {
            self.sockets.remove(handle);
            return Err(IoError(IO_REGISTER_FAILED));
        }
        Ok(handle)
    }

    fn open(&mut self, owner: Owner, path: &[u8], flags: u64) -> Result<Option<Reply>, IoError> {
        let endpoint = endpoint(path)?;
        if flags & !(READ | WRITE | BIND | LISTEN) != 0 {
            return Err(IoError(IO_INVALID_PATH));
        }
        if flags & (BIND | LISTEN) == 0
            && let Some(index) = self.resources.iter().position(|resource| {
                resource.owner == owner && matches!(resource.kind,
                    Kind::Connection { connecting: true, endpoint: candidate, .. } if candidate == endpoint)
            })
        {
                let Kind::Connection { socket, connecting, .. } = &mut self.resources[index].kind else { unreachable!() };
                let state = self.sockets.get::<tcp::Socket>(*socket).state();
                if matches!(state, tcp::State::Established | tcp::State::CloseWait) {
                    self.sockets.get_mut::<tcp::Socket>(*socket).set_timeout(None);
                    *connecting = false;
                    return Ok(Some(Reply::Value(self.resources[index].handle.0)));
                }
                if state == tcp::State::Closed {
                    let handle = self.resources[index].handle;
                    self.close(handle, owner, true)?;
                    return Err(IoError(IO_NO_DEVICE_FOUND));
                }
                return Ok(None);
        }
        if self.resources.len() == RESOURCE_LIMIT {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let handle = self.handle()?;
        if flags & (BIND | LISTEN) != 0 {
            if self.resources.iter().any(|resource| matches!(resource.kind, Kind::Listener { endpoint: candidate, .. } if candidate == endpoint)) {
                return Err(IoError(IO_REGISTER_FAILED));
            }
            let mut sockets = Vec::new();
            for _ in 0..BACKLOG {
                match self.listen_socket(endpoint) {
                    Ok(socket) => sockets.push(socket),
                    Err(error) => {
                        for socket in sockets {
                            self.sockets.remove(socket);
                        }
                        return Err(error);
                    }
                }
            }
            self.resources.push(Resource::new(
                handle,
                owner,
                Kind::Listener { endpoint, sockets },
            ));
            return Ok(Some(Reply::Value(handle.0)));
        }
        let local_port = (0..16384).find_map(|_| {
            let port = self.next_port;
            self.next_port = self.next_port.checked_add(1).unwrap_or(49152);
            (!self.sockets.iter().any(|(_, socket)| matches!(socket,
                smoltcp::socket::Socket::Tcp(socket) if socket.local_endpoint().is_some_and(|local| local.port == port)))).then_some(port)
        }).ok_or(IoError(IO_REGISTER_FAILED))?;
        let socket = self.new_socket()?;
        let local = IpEndpoint::new(endpoint.addr, local_port);
        if self
            .sockets
            .get_mut::<tcp::Socket>(socket)
            .connect(self.interface.context(), endpoint, local)
            .is_err()
        {
            self.sockets.remove(socket);
            return Err(IoError(IO_REGISTER_FAILED));
        }
        self.resources.push(Resource::new(
            handle,
            owner,
            Kind::Connection {
                socket,
                connecting: true,
                flags,
                endpoint,
            },
        ));
        Ok(None)
    }

    fn abort_socket(&mut self, socket: SocketHandle) {
        self.sockets.get_mut::<tcp::Socket>(socket).abort();
        self.closing.push(Closing::new(
            socket,
            self.now_usec.saturating_add(CLOSE_TIMEOUT_USEC),
        ));
    }

    fn close(&mut self, handle: Handle, owner: Owner, abort: bool) -> Result<(), IoError> {
        let index = self.index(handle, owner)?;
        match self.resources.remove(index).kind {
            Kind::Listener { sockets, .. } => {
                for socket in sockets {
                    if matches!(
                        self.sockets.get::<tcp::Socket>(socket).state(),
                        tcp::State::Listen | tcp::State::Closed
                    ) {
                        self.sockets.remove(socket);
                    } else {
                        self.abort_socket(socket);
                    }
                }
            }
            Kind::Connection { socket, .. } if abort => {
                self.abort_socket(socket);
            }
            Kind::Connection { socket, .. } => {
                self.sockets.get_mut::<tcp::Socket>(socket).close();
                self.closing.push(Closing::new(
                    socket,
                    self.now_usec.saturating_add(CLOSE_TIMEOUT_USEC),
                ));
            }
        }
        Ok(())
    }

    fn accept(&mut self, index: usize, owner: Owner) -> Result<Option<Reply>, IoError> {
        let Kind::Listener { endpoint, sockets } = &self.resources[index].kind else {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        };
        let Some(position) = sockets.iter().position(|socket| {
            matches!(
                self.sockets.get::<tcp::Socket>(*socket).state(),
                tcp::State::Established | tcp::State::CloseWait
            )
        }) else {
            return Ok(None);
        };
        if self.resources.len() == RESOURCE_LIMIT {
            return Err(IoError(IO_REGISTER_FAILED));
        }
        let endpoint = *endpoint;
        let handle = self.handle()?;
        let replacement = self.listen_socket(endpoint)?;
        let Kind::Listener { sockets, .. } = &mut self.resources[index].kind else {
            unreachable!()
        };
        let socket = core::mem::replace(&mut sockets[position], replacement);
        self.sockets
            .get_mut::<tcp::Socket>(socket)
            .set_timeout(None);
        self.resources.push(Resource::new(
            handle,
            owner,
            Kind::Connection {
                socket,
                connecting: false,
                flags: READ | WRITE,
                endpoint,
            },
        ));
        Ok(Some(Reply::Value(handle.0)))
    }
}

impl IoComponent for NetworkService {
    fn advance(&mut self, now_usec: u64) -> Schedule {
        self.now_usec = self.now_usec.max(now_usec);
        let now = self.instant();
        self.interface.poll_maintenance(now);
        let ingress = self
            .interface
            .poll_ingress_single(now, &mut self.device, &mut self.sockets);
        let egress = self
            .interface
            .poll_egress(now, &mut self.device, &mut self.sockets);
        for resource in &mut self.resources {
            if let Kind::Listener { endpoint, sockets } = &mut resource.kind {
                for handle in sockets {
                    let socket = self.sockets.get_mut::<tcp::Socket>(*handle);
                    if socket.state() == tcp::State::Closed {
                        socket
                            .listen(*endpoint)
                            .expect("Existing loopback listener invalid");
                    }
                }
            }
        }
        self.closing.retain(|closing| {
            if (self.sockets.get::<tcp::Socket>(closing.socket).state() == tcp::State::Closed
                && !self.device.has_input())
                || closing.deadline <= self.now_usec
            {
                self.sockets.remove(closing.socket);
                false
            } else {
                true
            }
        });
        let mut deadline = self
            .interface
            .poll_at(now, &self.sockets)
            .map(|instant| instant.total_micros().max(0) as u64);
        if let Some(closing) = self.closing.iter().map(|closing| closing.deadline).min() {
            deadline = Some(deadline.map_or(closing, |deadline| deadline.min(closing)));
        }
        if self.device.has_input() {
            deadline = Some(self.now_usec);
        }
        Schedule::new(
            ingress != PollIngressSingleResult::None || egress != PollResult::None,
            deadline,
        )
    }

    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        let nonblocking = request.transfer_flags & hunde::protocol::IO_TRANSFER_NONBLOCK != 0;
        if request.transfer_flags & !hunde::protocol::IO_TRANSFER_NONBLOCK != 0
            || (nonblocking
                && matches!(request.operation, Operation::Open(_, flags) if flags & (BIND | LISTEN) == 0))
        {
            return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
        }
        let result = (|| {
            let owner = Owner::new(request.session, request.pid);
            if owner.session.0 == 0 || owner.pid.0 == 0 {
                return Err(IoError(IO_PERMISSION_DENIED));
            }
            if let Operation::Open(path, flags) = request.operation {
                return self.open(owner, path, flags);
            }
            if matches!(request.operation, Operation::Release) {
                self.disconnect(request.session)?;
                return Ok(Some(Reply::Value(0)));
            }
            let index = self.index(request.handle, owner)?;
            if matches!(request.operation, Operation::Close) {
                self.close(request.handle, owner, false)?;
                return Ok(Some(Reply::Value(0)));
            }
            if matches!(request.operation, Operation::Accept) {
                return self.accept(index, owner);
            }
            let Kind::Connection { socket, flags, .. } = self.resources[index].kind else {
                return Err(IoError(IO_OPERATION_NOT_SUPPORTED));
            };
            let socket = self.sockets.get_mut::<tcp::Socket>(socket);
            let count = match request.operation {
                Operation::Read(_) if flags & READ == 0 => {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                Operation::Write(_) if flags & WRITE == 0 => {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                Operation::Read(target) => {
                    if target.is_empty() {
                        return Ok(Some(Reply::Value(0)));
                    }
                    if socket.state() == tcp::State::Closed {
                        return Err(IoError(IO_NOT_OPEN));
                    }
                    if !socket.can_recv() {
                        return Ok((!socket.may_recv()).then_some(Reply::Value(0)));
                    }
                    socket
                        .recv_slice(target)
                        .map_err(|_| IoError(IO_NOT_OPEN))?
                }
                Operation::Write(source) => {
                    if source.is_empty() {
                        return Ok(Some(Reply::Value(0)));
                    }
                    if !socket.may_send() {
                        return Err(IoError(IO_NOT_OPEN));
                    }
                    if !socket.can_send() {
                        return Ok(None);
                    }
                    socket
                        .send_slice(source)
                        .map_err(|_| IoError(IO_NOT_OPEN))?
                }
                _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
            };
            Ok(Some(Reply::Value(count as i64)))
        })();
        match result {
            Ok(None) if nonblocking => Err(IoError(hunde::protocol::IO_WOULD_BLOCK)),
            result => result,
        }
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        while let Some((handle, owner)) = self
            .resources
            .iter()
            .find(|resource| resource.owner.session == session)
            .map(|resource| (resource.handle, resource.owner))
        {
            self.close(handle, owner, true)?;
        }
        Ok(())
    }
}

#[cfg(feature = "component")]
hunde::io_component!(NetworkService, b"Rust network component panicked");

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::ProcessId;

    fn call(
        service: &mut NetworkService,
        session: u64,
        handle: i64,
        operation: Operation<'_>,
    ) -> Result<Option<i64>, IoError> {
        service
            .execute(Request::new(
                SessionId(session),
                ProcessId(session),
                Handle(handle),
                operation,
            ))
            .map(|reply| {
                reply.map(|reply| match reply {
                    Reply::Value(value) => value,
                    _ => panic!("Unexpected non-stream reply"),
                })
            })
    }

    fn pump(service: &mut NetworkService) {
        for _ in 0..256 {
            let schedule = service.advance(service.now_usec);
            if !schedule.worked() && !service.device.has_input() {
                return;
            }
        }
        panic!("Loopback did not become idle");
    }

    fn pair(service: &mut NetworkService, path: &[u8]) -> (i64, i64, i64) {
        let listener = call(service, 1, 0, Operation::Open(path, BIND | LISTEN))
            .unwrap()
            .unwrap();
        assert_eq!(call(service, 1, listener, Operation::Accept), Ok(None));
        assert_eq!(
            call(service, 2, 0, Operation::Open(path, READ | WRITE)),
            Ok(None)
        );
        let allocated = service.resources.len();
        assert_eq!(
            call(service, 2, 0, Operation::Open(path, READ | WRITE)),
            Ok(None)
        );
        assert_eq!(service.resources.len(), allocated);
        pump(service);
        let client = call(service, 2, 0, Operation::Open(path, READ | WRITE))
            .unwrap()
            .unwrap();
        let server = call(service, 1, listener, Operation::Accept)
            .unwrap()
            .unwrap();
        (listener, client, server)
    }

    fn read_exact(service: &mut NetworkService, session: u64, handle: i64, target: &mut [u8]) {
        let mut offset = 0;
        for _ in 0..2000 {
            if let Some(count) = call(
                service,
                session,
                handle,
                Operation::Read(&mut target[offset..]),
            )
            .unwrap()
            {
                assert!(count > 0);
                offset += count as usize;
                if offset == target.len() {
                    return;
                }
            }
            service.advance(service.now_usec + 1000);
        }
        panic!("TCP receive deadline exceeded");
    }

    #[test]
    fn ipv4_ipv6_and_localhost_use_tcp_with_partial_transfers_and_orderly_eof() {
        for path in [
            b"tcp://127.0.0.1:8080".as_slice(),
            b"tcp://[::1]:8080",
            b"tcp://localhost:8080",
        ] {
            let mut service = NetworkService::default();
            let (listener, client, server) = pair(&mut service, path);
            assert_eq!(
                call(&mut service, 1, server, Operation::Read(&mut [0; 10])),
                Ok(None)
            );
            let source: Vec<u8> = (0..8192).map(|index| (index % 251) as u8).collect();
            assert_eq!(
                call(&mut service, 2, client, Operation::Write(&source)),
                Ok(Some(BUFFER_SIZE as i64))
            );
            assert_eq!(
                call(&mut service, 2, client, Operation::Write(b"full")),
                Ok(None)
            );
            pump(&mut service);
            let mut received = vec![0; BUFFER_SIZE];
            read_exact(&mut service, 1, server, &mut received);
            assert_eq!(received, source[..BUFFER_SIZE]);
            assert_eq!(
                call(&mut service, 1, server, Operation::Write(b"pong")),
                Ok(Some(4))
            );
            pump(&mut service);
            let mut reply = [0; 4];
            read_exact(&mut service, 2, client, &mut reply);
            assert_eq!(&reply, b"pong");
            assert_eq!(call(&mut service, 2, client, Operation::Close), Ok(Some(0)));
            pump(&mut service);
            assert_eq!(
                call(&mut service, 1, server, Operation::Read(&mut reply)),
                Ok(Some(0))
            );
            assert_eq!(call(&mut service, 1, server, Operation::Close), Ok(Some(0)));
            call(&mut service, 1, listener, Operation::Close).unwrap();
            pump(&mut service);
            service.advance(CLOSE_TIMEOUT_USEC + 1);
            assert!(service.resources.is_empty());
            assert!(service.closing.is_empty());
            assert_eq!(service.sockets.iter().count(), 0);
        }
    }

    #[test]
    fn listener_replenishes_and_pending_connections_are_owned_by_the_listener() {
        let mut service = NetworkService::default();
        let path = b"tcp://127.0.0.1:8081";
        let (listener, client, server) = pair(&mut service, path);
        assert_eq!(
            call(&mut service, 2, listener, Operation::Accept),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 1, client, Operation::Close),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        assert_eq!(
            call(&mut service, 2, 0, Operation::Open(path, BIND)),
            Err(IoError(IO_REGISTER_FAILED))
        );
        for _ in 0..BACKLOG {
            assert_eq!(
                call(&mut service, 3, 0, Operation::Open(path, READ | WRITE)),
                Ok(None)
            );
            pump(&mut service);
            assert!(
                call(&mut service, 3, 0, Operation::Open(path, READ | WRITE))
                    .unwrap()
                    .is_some()
            );
        }
        for _ in 0..BACKLOG {
            assert!(
                call(&mut service, 1, listener, Operation::Accept)
                    .unwrap()
                    .is_some()
            );
        }
        assert_eq!(call(&mut service, 1, listener, Operation::Accept), Ok(None));
        service.disconnect(SessionId(1)).unwrap();
        assert_eq!(
            call(&mut service, 1, server, Operation::Close),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
        service.disconnect(SessionId(2)).unwrap();
        service.disconnect(SessionId(3)).unwrap();
        pump(&mut service);
        assert!(service.resources.is_empty());
        assert_eq!(service.sockets.iter().count(), 0);
        assert!(
            call(&mut service, 1, 0, Operation::Open(path, BIND))
                .unwrap()
                .is_some()
        );
    }

    #[test]
    fn refused_connect_releases_pending_open_without_duplicate_sockets() {
        let mut service = NetworkService::default();
        assert_eq!(
            call(
                &mut service,
                1,
                0,
                Operation::Open(b"tcp://localhost:9000", READ)
            ),
            Ok(None)
        );
        pump(&mut service);
        assert_eq!(
            call(
                &mut service,
                1,
                0,
                Operation::Open(b"tcp://localhost:9000", READ)
            ),
            Err(IoError(IO_NO_DEVICE_FOUND))
        );
        pump(&mut service);
        assert_eq!(service.sockets.iter().count(), 0);
        assert!(service.resources.is_empty());
    }

    #[test]
    fn closing_a_listener_resets_unaccepted_connections_but_preserves_accepted_ones() {
        let mut service = NetworkService::default();
        let path = b"tcp://localhost:8080";
        let (listener, client, server) = pair(&mut service, path);
        assert_eq!(
            call(&mut service, 3, 0, Operation::Open(path, READ)),
            Ok(None)
        );
        pump(&mut service);
        let unaccepted = call(&mut service, 3, 0, Operation::Open(path, READ))
            .unwrap()
            .unwrap();
        call(&mut service, 1, listener, Operation::Close).unwrap();
        pump(&mut service);
        assert_eq!(
            call(&mut service, 3, unaccepted, Operation::Read(&mut [0; 1])),
            Err(IoError(IO_NOT_OPEN))
        );
        assert_eq!(
            call(&mut service, 2, client, Operation::Write(b"ok")),
            Ok(Some(2))
        );
        pump(&mut service);
        let mut received = [0; 2];
        read_exact(&mut service, 1, server, &mut received);
        assert_eq!(&received, b"ok");
    }

    #[test]
    fn timer_wakeup_completes_close_without_another_application_request() {
        let mut service = NetworkService::default();
        let (_, client, _) = pair(&mut service, b"tcp://localhost:8080");
        call(&mut service, 2, client, Operation::Close).unwrap();
        pump(&mut service);
        let schedule = service.advance(1_000_000);
        assert!(
            schedule
                .deadline_usec()
                .is_some_and(|deadline| deadline <= CLOSE_TIMEOUT_USEC)
        );
        service.advance(CLOSE_TIMEOUT_USEC + 1);
        assert!(service.closing.is_empty());
    }

    #[test]
    fn a_lost_syn_recovers_on_its_deadline_without_an_incoming_packet_or_request() {
        use smoltcp::phy::{Device, RxToken};
        let mut service = NetworkService::default();
        let path = b"tcp://localhost:8080";
        call(&mut service, 1, 0, Operation::Open(path, BIND)).unwrap();
        assert_eq!(
            call(&mut service, 2, 0, Operation::Open(path, READ | WRITE)),
            Ok(None)
        );
        service.advance(0);
        let (syn, _) = service.device.receive(Instant::ZERO).unwrap();
        syn.consume(|packet| assert!(!packet.is_empty()));
        assert!(!service.device.has_input());
        let deadline = service.advance(0).deadline_usec().unwrap();
        assert!(deadline > 0);
        service.advance(deadline);
        assert!(service.device.has_input());
        pump(&mut service);
        assert!(
            call(&mut service, 2, 0, Operation::Open(path, READ | WRITE))
                .unwrap()
                .is_some()
        );
    }

    #[test]
    fn nonblocking_stream_operations_report_would_block_and_connect_is_explicitly_unsupported() {
        let mut service = NetworkService::default();
        let (listener, client, server) = pair(&mut service, b"tcp://localhost:8080");
        let pending = service.execute(
            Request::new(
                SessionId(1),
                ProcessId(1),
                Handle(listener),
                Operation::Accept,
            )
            .with_transfer_flags(hunde::protocol::IO_TRANSFER_NONBLOCK),
        );
        assert_eq!(pending, Err(IoError(hunde::protocol::IO_WOULD_BLOCK)));
        let pending = service.execute(
            Request::new(
                SessionId(1),
                ProcessId(1),
                Handle(server),
                Operation::Read(&mut [0; 1]),
            )
            .with_transfer_flags(hunde::protocol::IO_TRANSFER_NONBLOCK),
        );
        assert_eq!(pending, Err(IoError(hunde::protocol::IO_WOULD_BLOCK)));
        let resources = service.resources.len();
        let pending = service.execute(
            Request::new(
                SessionId(3),
                ProcessId(3),
                Handle(0),
                Operation::Open(b"tcp://localhost:8080", READ),
            )
            .with_transfer_flags(hunde::protocol::IO_TRANSFER_NONBLOCK),
        );
        assert_eq!(pending, Err(IoError(IO_OPERATION_NOT_SUPPORTED)));
        assert_eq!(service.resources.len(), resources);
        assert_eq!(
            call(&mut service, 2, client, Operation::Accept),
            Err(IoError(IO_OPERATION_NOT_SUPPORTED))
        );
    }

    #[test]
    fn invalid_endpoints_and_resource_exhaustion_fail_without_leaking_sockets() {
        let mut service = NetworkService::default();
        for path in [
            b"socket://name".as_slice(),
            b"tcp://192.0.2.1:80",
            b"tcp://localhost:0",
            b"tcp://localhost:65536",
            b"tcp://localhost:-1",
            b"tcp://127.0.0.1",
            b"tcp://[::]:80",
            b"tcp://localhost:80/extra",
        ] {
            assert_eq!(
                call(&mut service, 1, 0, Operation::Open(path, BIND)),
                Err(IoError(IO_INVALID_PATH))
            );
        }
        assert_eq!(
            call(
                &mut service,
                0,
                0,
                Operation::Open(b"tcp://localhost:80", BIND)
            ),
            Err(IoError(IO_PERMISSION_DENIED))
        );
        for port in 1000..1000 + SOCKET_LIMIT / BACKLOG {
            let path = alloc::format!("tcp://localhost:{port}");
            call(&mut service, 1, 0, Operation::Open(path.as_bytes(), BIND)).unwrap();
        }
        assert_eq!(service.sockets.iter().count(), SOCKET_LIMIT);
        assert_eq!(
            call(
                &mut service,
                1,
                0,
                Operation::Open(b"tcp://localhost:9000", BIND)
            ),
            Err(IoError(IO_REGISTER_FAILED))
        );
        service.disconnect(SessionId(1)).unwrap();
        assert_eq!(service.sockets.iter().count(), 0);
    }
}

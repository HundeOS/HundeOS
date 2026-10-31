#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::rc::Rc;
use alloc::vec;
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::boot::Uart;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{Handle, Operation, Reply, Request, SessionId};
use hunde::mmio::Mapping;
use hunde::protocol::*;
use hunde::service::{ServiceName, SharedService};
use hunde::terminal::Attributes;
use hunde::wuff::{DriverComponent, DriverProgress};

const HANDLE_LIMIT: usize = 64;
const OWNER_LIMIT: usize = 64;

trait Registers {
    fn read_u8(&self, offset: usize) -> Result<u8, IoError>;
    fn read_u32(&self, offset: usize) -> Result<u32, IoError>;
    fn write_u8(&mut self, offset: usize, value: u8) -> Result<(), IoError>;
    fn write_u32(&mut self, offset: usize, value: u32) -> Result<(), IoError>;
}

impl Registers for Mapping {
    fn read_u8(&self, offset: usize) -> Result<u8, IoError> {
        self.read_u8(offset)
    }
    fn read_u32(&self, offset: usize) -> Result<u32, IoError> {
        self.read_u32(offset)
    }
    fn write_u8(&mut self, offset: usize, value: u8) -> Result<(), IoError> {
        self.write_u8(offset, value)
    }
    fn write_u32(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
        self.write_u32(offset, value)
    }
}

struct Device<Register: Registers> {
    registers: Register,
    kind: u64,
    shift: u64,
    width: u64,
}

impl<Register: Registers> Device<Register> {
    fn new(registers: Register, configuration: Uart) -> Result<Self, IoError> {
        if !matches!(configuration.kind, UART_KIND_PL011 | UART_KIND_NS16550)
            || !matches!(configuration.register_width, 1 | 4)
            || configuration.register_shift > 5
        {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        Ok(Self {
            registers,
            kind: configuration.kind,
            shift: configuration.register_shift,
            width: configuration.register_width,
        })
    }

    fn ns_offset(&self, index: usize) -> usize {
        index << self.shift
    }

    fn can_transmit(&self) -> Result<bool, IoError> {
        if self.kind == UART_KIND_PL011 {
            Ok(self.registers.read_u32(0x18)? & (1 << 5) == 0)
        } else {
            let offset = self.ns_offset(5);
            let status = if self.width == 4 {
                self.registers.read_u32(offset)?
            } else {
                u32::from(self.registers.read_u8(offset)?)
            };
            Ok(status & (1 << 5) != 0)
        }
    }

    fn write(&mut self, value: u8) -> Result<(), IoError> {
        if self.kind == UART_KIND_PL011 {
            self.registers.write_u32(0, u32::from(value))
        } else {
            let offset = self.ns_offset(0);
            if self.width == 4 {
                self.registers.write_u32(offset, u32::from(value))
            } else {
                self.registers.write_u8(offset, value)
            }
        }
    }
}

struct Description {
    handle: Handle,
    owners: Vec<Owner>,
}

impl Description {
    fn new(handle: Handle, owner: Owner) -> Self {
        Self {
            handle,
            owners: vec![owner],
        }
    }
}

struct PendingWrite {
    session: SessionId,
    handle: Handle,
    offset: usize,
    carriage_return: bool,
}

impl PendingWrite {
    fn new(session: SessionId, handle: Handle) -> Self {
        Self {
            session,
            handle,
            offset: 0,
            carriage_return: false,
        }
    }
}

struct State<Register: Registers> {
    device: Device<Register>,
    attributes: Attributes,
    descriptions: Vec<Description>,
    grants: Grants,
    pending: Vec<PendingWrite>,
    next_handle: i64,
}

impl<Register: Registers> State<Register> {
    fn new(device: Device<Register>) -> Self {
        Self {
            device,
            attributes: Attributes::new(),
            descriptions: Vec::new(),
            grants: Grants::default(),
            pending: Vec::new(),
            next_handle: IO_SESSION_HANDLE_BASE as i64,
        }
    }

    fn write(
        &mut self,
        session: SessionId,
        handle: Handle,
        source: &[u8],
    ) -> Result<Option<Reply>, IoError> {
        if source.is_empty() {
            return Ok(Some(Reply::Value(0)));
        }
        let index = match self
            .pending
            .iter()
            .position(|write| write.session == session)
        {
            Some(index) => index,
            None => {
                self.pending.push(PendingWrite::new(session, handle));
                self.pending.len() - 1
            }
        };
        let write = &mut self.pending[index];
        if write.handle != handle || write.offset > source.len() {
            return Err(IoError(IO_UNKNOWN_HANDLE));
        }
        if !self.device.can_transmit()? {
            return Ok(None);
        }
        if write.carriage_return {
            self.device.write(b'\r')?;
            write.carriage_return = false;
        } else {
            let value = source[write.offset];
            self.device.write(value)?;
            write.offset += 1;
            write.carriage_return = value == b'\n';
        }
        if write.offset == source.len() && !write.carriage_return {
            self.pending.remove(index);
            Ok(Some(Reply::Value(source.len() as i64)))
        } else {
            Ok(None)
        }
    }

    fn reap(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
}

impl<Register: Registers + 'static> IoComponent for State<Register> {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        let reply = match request.operation {
            Operation::Open(path, flags) => {
                if path.len() < 3
                    || path.first() != Some(&b':')
                    || path.last() != Some(&b':')
                    || path[1..path.len() - 1].iter().any(|value| {
                        matches!(value, b':' | b'/' | 0 | b' ' | b'\t' | b'\r' | b'\n')
                    })
                {
                    return Err(IoError(IO_INVALID_PATH));
                }
                if flags != IO_OPEN_WRITE {
                    return Err(IoError(IO_PERMISSION_DENIED));
                }
                if self.descriptions.len() == HANDLE_LIMIT {
                    return Err(IoError(IO_REGISTER_FAILED));
                }
                let handle = Handle(self.next_handle);
                self.next_handle = self
                    .next_handle
                    .checked_add(1)
                    .ok_or(IoError(IO_REGISTER_FAILED))?;
                self.descriptions.push(Description::new(handle, owner));
                Reply::Value(handle.0)
            }
            Operation::InheritCommit { token, target } => {
                self.grants.commit(token, owner, target)?;
                Reply::Value(0)
            }
            Operation::InheritAbort(token) => {
                self.grants.abort(token, owner)?;
                self.reap();
                Reply::Value(0)
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
                Reply::Value(grant.handle.0)
            }
            Operation::Release => {
                self.disconnect(request.session)?;
                Reply::Value(0)
            }
            _ => {
                let index = self
                    .descriptions
                    .iter()
                    .position(|description| {
                        description.handle == request.handle && description.owners.contains(&owner)
                    })
                    .ok_or(IoError(IO_UNKNOWN_HANDLE))?;
                match request.operation {
                    Operation::Write(source) => {
                        return self.write(request.session, request.handle, source);
                    }
                    Operation::GetTerminalAttributes(attributes) => {
                        *attributes = self.attributes;
                        Reply::Value(0)
                    }
                    Operation::SetTerminalAttributes(attributes) => {
                        self.attributes = attributes;
                        Reply::Value(0)
                    }
                    Operation::InheritPrepare => {
                        return self
                            .grants
                            .prepare(request.handle, owner, IO_OPEN_WRITE)
                            .map(Some);
                    }
                    Operation::Close => {
                        self.descriptions[index]
                            .owners
                            .retain(|candidate| *candidate != owner);
                        self.grants.close(request.handle, owner);
                        self.reap();
                        Reply::Value(0)
                    }
                    _ => return Err(IoError(IO_OPERATION_NOT_SUPPORTED)),
                }
            }
        };
        Ok(Some(reply))
    }

    fn disconnect(&mut self, session: SessionId) -> Result<(), IoError> {
        self.pending.retain(|write| write.session != session);
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.grants.release(session);
        self.reap();
        Ok(())
    }
}

pub struct Transmitter {
    state: Rc<RefCell<State<Mapping>>>,
    service: SharedService<State<Mapping>>,
}

impl DriverComponent for Transmitter {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let configuration = Uart::from_wire_bytes(configuration)?;
        if configuration.mmio_size == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let size = usize::try_from(configuration.mmio_size)
            .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        let mapping = Mapping::map(configuration.physical_base, size)?;
        let device = Device::new(mapping, configuration)?;
        let state = Rc::new(RefCell::new(State::new(device)));
        let service = SharedService::listen(ServiceName::new("uart_tx"), state.clone())?;
        Ok(Self { state, service })
    }

    fn irq(&self) -> Option<u64> {
        None
    }

    fn step(&mut self) -> Result<DriverProgress, IoError> {
        let worked = self.service.poll();
        let pending = !self.state.borrow().pending.is_empty();
        Ok(DriverProgress::new(worked || pending, false))
    }

    fn deliver(&mut self) {}
}

#[cfg(test)]
mod tests {
    use super::*;
    use alloc::collections::VecDeque;
    use hunde::io::ProcessId;

    struct FakeRegisters {
        ready: RefCell<VecDeque<bool>>,
        writes: RefCell<Vec<(usize, u32)>>,
    }

    impl FakeRegisters {
        fn new(ready: &[bool]) -> Self {
            Self {
                ready: RefCell::new(ready.iter().copied().collect()),
                writes: RefCell::new(Vec::new()),
            }
        }
    }

    impl Registers for FakeRegisters {
        fn read_u8(&self, _offset: usize) -> Result<u8, IoError> {
            Ok(u8::from(self.ready.borrow_mut().pop_front().unwrap_or(true)) << 5)
        }

        fn read_u32(&self, offset: usize) -> Result<u32, IoError> {
            let ready = self.ready.borrow_mut().pop_front().unwrap_or(true);
            Ok(if offset == 0x18 {
                u32::from(!ready) << 5
            } else {
                u32::from(ready) << 5
            })
        }

        fn write_u8(&mut self, offset: usize, value: u8) -> Result<(), IoError> {
            self.writes.borrow_mut().push((offset, u32::from(value)));
            Ok(())
        }

        fn write_u32(&mut self, offset: usize, value: u32) -> Result<(), IoError> {
            self.writes.borrow_mut().push((offset, value));
            Ok(())
        }
    }

    fn configuration(kind: u64, width: u64, shift: u64) -> Uart {
        let mut configuration = Uart::new();
        configuration.kind = kind;
        configuration.register_width = width;
        configuration.register_shift = shift;
        configuration
    }

    fn open(state: &mut State<FakeRegisters>, session: SessionId, pid: ProcessId) -> Handle {
        let reply = state
            .execute(Request::new(
                session,
                pid,
                Handle(0),
                Operation::Open(b":backend:", IO_OPEN_WRITE),
            ))
            .unwrap();
        let Some(Reply::Value(handle)) = reply else {
            panic!("Expected handle")
        };
        Handle(handle)
    }

    #[test]
    fn pl011_write_waits_for_space_and_completes_after_carriage_return() {
        let device = Device::new(
            FakeRegisters::new(&[false, true, false, false, true]),
            configuration(UART_KIND_PL011, 4, 0),
        )
        .unwrap();
        let mut state = State::new(device);
        let session = SessionId(2);
        let pid = ProcessId(7);
        let handle = open(&mut state, session, pid);
        let write = || Request::new(session, pid, handle, Operation::Write(b"\n"));
        assert_eq!(state.execute(write()), Ok(None));
        assert_eq!(state.execute(write()), Ok(None));
        assert_eq!(state.execute(write()), Ok(None));
        assert_eq!(state.execute(write()), Ok(None));
        assert_eq!(state.execute(write()), Ok(Some(Reply::Value(1))));
        assert_eq!(*state.device.registers.writes.borrow(), [(0, 10), (0, 13)]);
        assert!(state.pending.is_empty());
    }

    #[test]
    fn ns16550_uses_register_width_and_shift() {
        let device = Device::new(
            FakeRegisters::new(&[]),
            configuration(UART_KIND_NS16550, 4, 2),
        )
        .unwrap();
        let mut state = State::new(device);
        let session = SessionId(3);
        let pid = ProcessId(8);
        let handle = open(&mut state, session, pid);
        assert_eq!(
            state.execute(Request::new(session, pid, handle, Operation::Write(b"A"))),
            Ok(Some(Reply::Value(1)))
        );
        assert_eq!(*state.device.registers.writes.borrow(), [(0, 65)]);
    }

    #[test]
    fn disconnect_cancels_pending_and_rejects_stale_handle() {
        let device = Device::new(
            FakeRegisters::new(&[false]),
            configuration(UART_KIND_PL011, 4, 0),
        )
        .unwrap();
        let mut state = State::new(device);
        let session = SessionId(4);
        let pid = ProcessId(9);
        let handle = open(&mut state, session, pid);
        assert_eq!(
            state.execute(Request::new(session, pid, handle, Operation::Write(b"B"))),
            Ok(None)
        );
        assert_eq!(state.pending.len(), 1);
        state.disconnect(session).unwrap();
        assert!(state.pending.is_empty());
        assert_eq!(
            state.execute(Request::new(session, pid, handle, Operation::Write(b"B"))),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }
}

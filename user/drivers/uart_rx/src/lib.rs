#![cfg_attr(feature = "component", no_std)]

extern crate alloc;

use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::vec;
use alloc::vec::Vec;
use core::cell::RefCell;
use hunde::abi::IoError;
use hunde::boot::Uart;
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{Handle, Operation, ProcessId, Reply, Request, SessionId};
use hunde::mmio::Mapping;
use hunde::protocol::*;
use hunde::service::{ServiceName, SharedService};
use hunde::terminal::Attributes;
use hunde::wuff::{DriverComponent, DriverProgress};

const CAPACITY: usize = 1024;
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

    fn read_ns(&self, index: usize) -> Result<u32, IoError> {
        let offset = self.ns_offset(index);
        if self.width == 4 {
            self.registers.read_u32(offset)
        } else {
            self.registers.read_u8(offset).map(u32::from)
        }
    }

    fn write_ns(&mut self, index: usize, value: u8) -> Result<(), IoError> {
        let offset = self.ns_offset(index);
        if self.width == 4 {
            self.registers.write_u32(offset, u32::from(value))
        } else {
            self.registers.write_u8(offset, value)
        }
    }

    fn configure(&mut self) -> Result<(), IoError> {
        if self.kind == UART_KIND_PL011 {
            self.registers.write_u32(0x38, 0)?;
            self.registers
                .write_u32(0x44, (1 << 4) | (1 << 6) | (0xf << 7))?;
            self.registers
                .write_u32(0x30, (1 << 0) | (1 << 8) | (1 << 9))
        } else {
            self.write_ns(1, 0)
        }
    }

    fn enable_interrupt(&mut self) -> Result<(), IoError> {
        if self.kind == UART_KIND_PL011 {
            self.registers.write_u32(0x38, (1 << 4) | (1 << 6))
        } else {
            self.write_ns(1, 1)
        }
    }

    fn acknowledge(&mut self) -> Result<(), IoError> {
        if self.kind == UART_KIND_PL011 {
            self.registers
                .write_u32(0x44, (1 << 4) | (1 << 6) | (0xf << 7))
        } else if self.read_ns(2)? & 0x0f == 0x07 {
            self.read_ns(31)?;
            Ok(())
        } else {
            Ok(())
        }
    }

    fn can_receive(&self) -> Result<bool, IoError> {
        if self.kind == UART_KIND_PL011 {
            Ok(self.registers.read_u32(0x18)? & (1 << 4) == 0)
        } else {
            Ok(self.read_ns(5)? & 1 != 0)
        }
    }

    fn read(&self) -> Result<u8, IoError> {
        if self.kind == UART_KIND_PL011 {
            Ok(self.registers.read_u32(0)? as u8)
        } else {
            Ok(self.read_ns(0)? as u8)
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

struct State<Register: Registers> {
    device: Device<Register>,
    input: VecDeque<u8>,
    foreground: Option<ProcessId>,
    interrupted: Option<ProcessId>,
    pending_irq: bool,
    attributes: Attributes,
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

impl<Register: Registers> State<Register> {
    fn new(device: Device<Register>) -> Self {
        Self {
            device,
            input: VecDeque::new(),
            foreground: None,
            interrupted: None,
            pending_irq: false,
            attributes: Attributes::new(),
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: IO_SESSION_HANDLE_BASE as i64,
        }
    }

    fn drain(
        &mut self,
        mut interrupt: impl FnMut(ProcessId) -> Result<(), IoError>,
    ) -> Result<bool, IoError> {
        let mut progress = false;
        while self.input.len() < CAPACITY && self.device.can_receive()? {
            let value = self.device.read()?;
            if value == 3
                && let Some(pid) = self.foreground
            {
                if interrupt(pid).is_ok() {
                    self.interrupted = Some(pid);
                }
                progress = true;
                continue;
            }
            self.input.push_back(value);
            progress = true;
        }
        Ok(progress)
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
                if flags != IO_OPEN_READ {
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
                    Operation::Read(buffer) => {
                        if self.interrupted == Some(request.pid) {
                            self.interrupted = None;
                            return Err(IoError(IO_INTERRUPTED));
                        }
                        if self.input.is_empty() && !buffer.is_empty() {
                            return Ok(None);
                        }
                        let count = buffer.len().min(self.input.len());
                        for target in &mut buffer[..count] {
                            *target = self.input.pop_front().ok_or(IoError(IO_PANIC))?;
                        }
                        Reply::Value(count as i64)
                    }
                    Operation::GetTerminalAttributes(attributes) => {
                        *attributes = self.attributes;
                        Reply::Value(0)
                    }
                    Operation::SetTerminalAttributes(attributes) => {
                        self.attributes = attributes;
                        Reply::Value(0)
                    }
                    Operation::SetForegroundProcess(pid) => {
                        self.foreground = (pid.0 != 0).then_some(pid);
                        self.interrupted = None;
                        Reply::Value(0)
                    }
                    Operation::InheritPrepare => {
                        return self
                            .grants
                            .prepare(request.handle, owner, IO_OPEN_READ)
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
        for description in &mut self.descriptions {
            description.owners.retain(|owner| owner.session != session);
        }
        self.grants.release(session);
        self.reap();
        Ok(())
    }
}

pub struct Receiver {
    irq: u64,
    state: Rc<RefCell<State<Mapping>>>,
    service: SharedService<State<Mapping>>,
}

impl DriverComponent for Receiver {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let configuration = Uart::from_wire_bytes(configuration)?;
        if configuration.irq == 0 || configuration.mmio_size == 0 {
            return Err(IoError(SERVICE_INVALID_BUFFER));
        }
        let size = usize::try_from(configuration.mmio_size)
            .map_err(|_| IoError(SERVICE_INVALID_BUFFER))?;
        let mapping = Mapping::map(configuration.physical_base, size)?;
        let mut device = Device::new(mapping, configuration)?;
        device.configure()?;
        let state = Rc::new(RefCell::new(State::new(device)));
        let service = SharedService::listen(ServiceName::new("uart_rx"), state.clone())?;
        state.borrow_mut().device.enable_interrupt()?;
        Ok(Self {
            irq: configuration.irq,
            state,
            service,
        })
    }

    fn irq(&self) -> Option<u64> {
        Some(self.irq)
    }

    fn step(&mut self) -> Result<DriverProgress, IoError> {
        let mut worked = self.service.poll();
        let mut state = self.state.borrow_mut();
        worked |= state.drain(hunde::terminal::interrupt)?;
        let rearm = state.pending_irq && !state.device.can_receive()?;
        if rearm {
            state.device.acknowledge()?;
            state.pending_irq = false;
            worked = true;
        }
        Ok(DriverProgress::new(worked, rearm))
    }

    fn deliver(&mut self) {
        self.state.borrow_mut().pending_irq = true;
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    struct FakeRegisters {
        input: RefCell<VecDeque<u8>>,
        writes: RefCell<Vec<(usize, u32)>>,
    }

    impl FakeRegisters {
        fn new(bytes: &[u8]) -> Self {
            Self {
                input: RefCell::new(bytes.iter().copied().collect()),
                writes: RefCell::new(Vec::new()),
            }
        }
    }

    impl Registers for FakeRegisters {
        fn read_u8(&self, offset: usize) -> Result<u8, IoError> {
            Ok(match offset {
                0 => self.input.borrow_mut().pop_front().unwrap_or_default(),
                5 => u8::from(!self.input.borrow().is_empty()),
                _ => 0,
            })
        }

        fn read_u32(&self, offset: usize) -> Result<u32, IoError> {
            Ok(match offset {
                0 => u32::from(self.input.borrow_mut().pop_front().unwrap_or_default()),
                0x14 => u32::from(!self.input.borrow().is_empty()),
                0x18 => u32::from(self.input.borrow().is_empty()) << 4,
                _ => 0,
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

    #[test]
    fn pl011_configuration_drain_and_acknowledgement() {
        let registers = FakeRegisters::new(b"abc");
        let device = Device::new(registers, configuration(UART_KIND_PL011, 4, 0)).unwrap();
        let mut state = State::new(device);
        state.device.configure().unwrap();
        state.device.enable_interrupt().unwrap();
        assert!(state.drain(|_| Ok(())).unwrap());
        assert_eq!(state.input.iter().copied().collect::<Vec<_>>(), b"abc");
        assert!(!state.device.can_receive().unwrap());
        state.device.acknowledge().unwrap();
        assert_eq!(
            state.device.registers.writes.borrow().last(),
            Some(&(0x44, 0x7d0))
        );
    }

    #[test]
    fn ns16550_register_spacing_and_ctrl_c_signal() {
        let registers = FakeRegisters::new(b"x\x03y");
        let device = Device::new(registers, configuration(UART_KIND_NS16550, 4, 2)).unwrap();
        let mut state = State::new(device);
        state.foreground = Some(ProcessId(73));
        state.device.configure().unwrap();
        state.device.enable_interrupt().unwrap();
        let mut signalled = Vec::new();
        assert!(
            state
                .drain(|pid| {
                    signalled.push(pid);
                    Ok(())
                })
                .unwrap()
        );
        assert_eq!(signalled, [ProcessId(73)]);
        assert_eq!(state.interrupted, Some(ProcessId(73)));
        assert_eq!(state.input.iter().copied().collect::<Vec<_>>(), b"xy");
        assert_eq!(*state.device.registers.writes.borrow(), [(4, 0), (4, 1)]);
    }

    #[test]
    fn stream_waits_for_input_and_reports_interrupt_once() {
        let device = Device::new(
            FakeRegisters::new(b""),
            configuration(UART_KIND_PL011, 4, 0),
        )
        .unwrap();
        let mut state = State::new(device);
        let session = SessionId(2);
        let pid = ProcessId(7);
        let open = Request::new(
            session,
            pid,
            Handle(0),
            Operation::Open(b":backend:", IO_OPEN_READ),
        );
        let Some(Reply::Value(handle)) = state.execute(open).unwrap() else {
            panic!("Expected handle")
        };
        let mut buffer = [0; 4];
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut buffer)
            )),
            Ok(None)
        );
        state.input.push_back(b'Q');
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut buffer)
            )),
            Ok(Some(Reply::Value(1)))
        );
        assert_eq!(buffer[0], b'Q');
        state.interrupted = Some(pid);
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut buffer)
            )),
            Err(IoError(IO_INTERRUPTED))
        );
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut buffer)
            )),
            Ok(None)
        );
    }
}

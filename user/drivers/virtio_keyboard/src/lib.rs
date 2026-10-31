#![no_std]

extern crate alloc;

mod device;

use alloc::collections::VecDeque;
use alloc::rc::Rc;
use alloc::vec;
use alloc::vec::Vec;
use core::cell::RefCell;
use device::Device;
use hunde::abi::IoError;
use hunde::boot::{VirtioDeviceConfiguration, VirtioMmio};
use hunde::component::IoComponent;
use hunde::inheritance::{Grants, Owner};
use hunde::io::{Handle, Operation, Reply, Request, SessionId};
use hunde::protocol::*;
use hunde::service::{ServiceName, SharedService};
use hunde::terminal::Attributes;
use hunde::wuff::{DriverComponent, DriverProgress};

const INPUT_CAPACITY: usize = 256;
const HANDLE_LIMIT: usize = 64;
const OWNER_LIMIT: usize = 64;

struct Input {
    pending: VecDeque<u8>,
    shift: bool,
}

impl Input {
    fn new() -> Self {
        Self {
            pending: VecDeque::new(),
            shift: false,
        }
    }

    fn has_room_for_event(&self) -> bool {
        self.pending.len() <= INPUT_CAPACITY - 3
    }

    fn emit(&mut self, byte: u8) {
        if self.pending.len() < INPUT_CAPACITY {
            self.pending.push_back(byte);
        }
    }

    fn escape(&mut self, final_byte: u8) {
        self.emit(0x1b);
        self.emit(b'[');
        self.emit(final_byte);
    }

    fn letter(code: u16, shift: bool) -> Option<u8> {
        let letter = match code {
            16..=25 => b"qwertyuiop"[usize::from(code - 16)],
            30..=38 => b"asdfghjkl"[usize::from(code - 30)],
            44..=50 => b"zxcvbnm"[usize::from(code - 44)],
            _ => return None,
        };
        Some(if shift {
            letter.to_ascii_uppercase()
        } else {
            letter
        })
    }

    fn number_or_symbol(code: u16, shift: bool) -> Option<u8> {
        if !(2..=11).contains(&code) {
            return None;
        }
        let index = usize::from(code - 2);
        Some(if shift {
            b"!@#$%^&*()"[index]
        } else {
            b"1234567890"[index]
        })
    }

    fn punctuation(code: u16, shift: bool) -> Option<u8> {
        let pair = match code {
            12 => (b'-', b'_'),
            13 => (b'=', b'+'),
            26 => (b'[', b'{'),
            27 => (b']', b'}'),
            39 => (b';', b':'),
            40 => (b'\'', b'"'),
            41 => (b'`', b'~'),
            43 => (b'\\', b'|'),
            51 => (b',', b'<'),
            52 => (b'.', b'>'),
            53 => (b'/', b'?'),
            _ => return None,
        };
        Some(if shift { pair.1 } else { pair.0 })
    }

    fn key(&mut self, code: u16, value: i32) {
        let pressed = value == 1 || value == 2;
        if code == 42 || code == 54 {
            self.shift = pressed;
            return;
        }
        if !pressed {
            return;
        }
        match code {
            28 => self.emit(b'\n'),
            14 => self.emit(0x7f),
            15 => self.emit(b'\t'),
            57 => self.emit(b' '),
            103 => self.escape(b'A'),
            108 => self.escape(b'B'),
            106 => self.escape(b'C'),
            105 => self.escape(b'D'),
            _ => {
                if let Some(byte) = Self::letter(code, self.shift)
                    .or_else(|| Self::number_or_symbol(code, self.shift))
                    .or_else(|| Self::punctuation(code, self.shift))
                {
                    self.emit(byte);
                }
            }
        }
    }

    fn read(&mut self, buffer: &mut [u8]) -> Option<usize> {
        if self.pending.is_empty() && !buffer.is_empty() {
            return None;
        }
        let count = buffer.len().min(self.pending.len());
        for target in &mut buffer[..count] {
            *target = self.pending.pop_front().unwrap_or_default();
        }
        Some(count)
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

struct State {
    input: Input,
    attributes: Attributes,
    descriptions: Vec<Description>,
    grants: Grants,
    next_handle: i64,
}

impl State {
    fn new() -> Self {
        Self {
            input: Input::new(),
            attributes: Attributes::new(),
            descriptions: Vec::new(),
            grants: Grants::default(),
            next_handle: IO_SESSION_HANDLE_BASE as i64,
        }
    }

    fn reap(&mut self) {
        self.descriptions.retain(|description| {
            !description.owners.is_empty() || self.grants.retains(description.handle)
        });
    }
}

impl IoComponent for State {
    fn execute(&mut self, request: Request<'_>) -> Result<Option<Reply>, IoError> {
        if request.session.0 == 0 || request.pid.0 == 0 {
            return Err(IoError(IO_PERMISSION_DENIED));
        }
        let owner = Owner::new(request.session, request.pid);
        let reply = match request.operation {
            Operation::Open(path, flags) => {
                if path != b":keyboard:" && path != b":backend:" {
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
                    Operation::Read(buffer) => match self.input.read(buffer) {
                        Some(count) => Reply::Value(count as i64),
                        None => return Ok(None),
                    },
                    Operation::GetTerminalAttributes(attributes) => {
                        *attributes = self.attributes;
                        Reply::Value(0)
                    }
                    Operation::SetTerminalAttributes(attributes) => {
                        self.attributes = attributes;
                        Reply::Value(0)
                    }
                    Operation::SetForegroundProcess(_) => Reply::Value(0),
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

pub struct Keyboard {
    device: Device,
    state: Rc<RefCell<State>>,
    service: SharedService<State>,
    pending_irq: bool,
}

impl DriverComponent for Keyboard {
    fn load(configuration: &[u8]) -> Result<Self, IoError> {
        let configuration = VirtioDeviceConfiguration::from_wire_bytes(configuration)?;
        let device = Device::start(configuration)?;
        let state = Rc::new(RefCell::new(State::new()));
        let service = SharedService::listen(ServiceName::new("keyboard"), state.clone())?;
        Ok(Self {
            device,
            state,
            service,
            pending_irq: false,
        })
    }

    fn irq(&self) -> Option<u64> {
        Some(self.device.irq())
    }

    fn step(&mut self) -> Result<DriverProgress, IoError> {
        let mut worked = self.service.poll();
        let (drained, empty) = self.device.drain(&mut self.state.borrow_mut().input)?;
        worked |= drained;
        let rearm = self.pending_irq && empty;
        if rearm {
            self.device.acknowledge()?;
            self.pending_irq = false;
            worked = true;
        }
        Ok(DriverProgress::new(worked, rearm))
    }

    fn deliver(&mut self) {
        self.pending_irq = true;
    }
}

pub fn probe(hardware: VirtioMmio) -> Result<Option<u64>, IoError> {
    device::probe(hardware)
}

#[cfg(test)]
mod tests {
    use super::*;
    use hunde::io::{ProcessId, SessionId};

    #[test]
    fn translates_key_press_shift_and_arrows() {
        let mut input = Input::new();
        input.key(30, 1);
        input.key(48, 1);
        input.key(42, 1);
        input.key(46, 1);
        input.key(42, 0);
        input.key(28, 1);
        input.key(14, 1);
        input.key(105, 1);
        input.key(30, 0);
        let mut first = [0; 3];
        assert_eq!(input.read(&mut first), Some(3));
        assert_eq!(&first, b"abC");
        let mut remaining = [0; 8];
        assert_eq!(input.read(&mut remaining), Some(5));
        assert_eq!(&remaining[..5], b"\n\x7f\x1b[D");
    }

    #[test]
    fn keyboard_probe_requires_virtio_input_device() {
        assert!(Device::is_keyboard(0x7472_6976, 1, 18));
        assert!(!Device::is_keyboard(0x7472_6976, 1, 2));
        assert!(!Device::is_keyboard(0, 1, 18));
    }

    #[test]
    fn empty_reads_wait_and_full_buffer_preserves_event_room() {
        let mut input = Input::new();
        assert_eq!(input.read(&mut [0; 1]), None);
        assert_eq!(input.read(&mut []), Some(0));
        for _ in 0..254 {
            input.emit(b'x');
        }
        assert!(!input.has_room_for_event());
        let mut bytes = [0; 254];
        assert_eq!(input.read(&mut bytes), Some(254));
        assert!(input.has_room_for_event());
    }

    #[test]
    fn stream_requires_owned_read_handle() {
        let mut state = State::new();
        let session = SessionId(7);
        let pid = ProcessId(9);
        let opened = state
            .execute(Request::new(
                session,
                pid,
                Handle(0),
                Operation::Open(b":keyboard:", IO_OPEN_READ),
            ))
            .unwrap();
        let Some(Reply::Value(handle)) = opened else {
            panic!("expected handle")
        };
        let mut attributes = Attributes::new();
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::GetTerminalAttributes(&mut attributes),
            )),
            Ok(Some(Reply::Value(0)))
        );
        attributes.local_flags = 0;
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::SetTerminalAttributes(attributes),
            )),
            Ok(Some(Reply::Value(0)))
        );
        let mut bytes = [0; 2];
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut bytes),
            )),
            Ok(None)
        );
        state.input.key(30, 1);
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(handle),
                Operation::Read(&mut bytes),
            )),
            Ok(Some(Reply::Value(1)))
        );
        assert_eq!(bytes[0], b'a');
        assert_eq!(
            state.execute(Request::new(
                SessionId(8),
                pid,
                Handle(handle),
                Operation::Read(&mut bytes),
            )),
            Err(IoError(IO_UNKNOWN_HANDLE))
        );
    }

    #[test]
    fn console_can_open_keyboard_backend() {
        let mut state = State::new();
        let session = SessionId(7);
        let pid = ProcessId(9);
        assert!(matches!(
            state.execute(Request::new(
                session,
                pid,
                Handle(0),
                Operation::Open(b":backend:", IO_OPEN_READ),
            )),
            Ok(Some(Reply::Value(_)))
        ));
        assert_eq!(
            state.execute(Request::new(
                session,
                pid,
                Handle(0),
                Operation::Open(b":invalid:", IO_OPEN_READ),
            )),
            Err(IoError(IO_INVALID_PATH))
        );
    }
}

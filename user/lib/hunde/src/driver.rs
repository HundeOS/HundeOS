use crate::abi::{ByteSpan, IoError, StatusResult, host::NativeLayout};
use crate::native::Storage;
use crate::protocol::*;
use alloc::rc::Rc;
use core::ffi::c_void;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Event {
    pub irq: u64,
    pub sequence: u64,
    pub kernel_at_usec: u64,
    pub manager_at_usec: u64,
}

impl Event {
    pub fn new(irq: u64, sequence: u64, kernel_at_usec: u64, manager_at_usec: u64) -> Self {
        Self {
            irq,
            sequence,
            kernel_at_usec,
            manager_at_usec,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Operation {
    Subscribe,
    Wait,
    Acknowledge,
    Invalid,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Request {
    pub operation: Operation,
    pub irq: u64,
    pub generation: u64,
    pub sequence: u64,
}

impl Request {
    pub fn new(operation: Operation, irq: u64, generation: u64, sequence: u64) -> Self {
        Self {
            operation,
            irq,
            generation,
            sequence,
        }
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Response {
    kind: u32,
    code: i32,
    irq: u64,
    generation: u64,
    sequence: u64,
    kernel_at_usec: u64,
    manager_at_usec: u64,
}

impl Response {
    pub fn subscribed(irq: u64, generation: u64) -> Self {
        Self {
            kind: INTERRUPT_PROTOCOL_SUBSCRIBED,
            code: INTERRUPT_PROTOCOL_OK,
            irq,
            generation,
            sequence: 0,
            kernel_at_usec: 0,
            manager_at_usec: 0,
        }
    }
    pub fn delivery(event: Event, generation: u64) -> Self {
        Self {
            kind: INTERRUPT_PROTOCOL_DELIVERY,
            code: INTERRUPT_PROTOCOL_OK,
            irq: event.irq,
            generation,
            sequence: event.sequence,
            kernel_at_usec: event.kernel_at_usec,
            manager_at_usec: event.manager_at_usec,
        }
    }
    pub fn acknowledged(request: Request) -> Self {
        Self {
            kind: INTERRUPT_PROTOCOL_ACKNOWLEDGED,
            code: INTERRUPT_PROTOCOL_OK,
            irq: request.irq,
            generation: request.generation,
            sequence: request.sequence,
            kernel_at_usec: 0,
            manager_at_usec: 0,
        }
    }
    pub fn error(irq: u64, code: i32) -> Self {
        Self {
            kind: INTERRUPT_PROTOCOL_ERROR,
            code,
            irq,
            generation: 0,
            sequence: 0,
            kernel_at_usec: 0,
            manager_at_usec: 0,
        }
    }
}

#[repr(C)]
struct NativeValue {
    status: StatusResult,
    value: u64,
}

#[repr(C)]
struct NativeEvent {
    status: StatusResult,
    irq: u64,
    sequence: u64,
    kernel_at_usec: u64,
    manager_at_usec: u64,
}

#[repr(C)]
struct NativeRequest {
    status: StatusResult,
    kind: u32,
    reserved: u32,
    irq: u64,
    generation: u64,
    sequence: u64,
}

impl NativeRequest {
    fn into_result(self) -> Result<Option<Request>, IoError> {
        if self.status.kind == crate::abi::PENDING {
            return Ok(None);
        }
        self.status.into_result()?;
        let operation = match self.kind {
            INTERRUPT_PROTOCOL_SUBSCRIBE => Operation::Subscribe,
            INTERRUPT_PROTOCOL_WAIT => Operation::Wait,
            INTERRUPT_PROTOCOL_ACKNOWLEDGE => Operation::Acknowledge,
            _ => Operation::Invalid,
        };
        Ok(Some(Request::new(
            operation,
            self.irq,
            self.generation,
            self.sequence,
        )))
    }
}

pub struct Manager {
    storage: Storage,
}

impl Manager {
    pub fn register() -> Result<Rc<Self>, IoError> {
        let storage = Storage::new(unsafe { hunde_driver_manager_layout() })?;
        unsafe { hunde_driver_manager_init(storage.as_ptr()) }.into_result()?;
        Ok(Rc::new(Self { storage }))
    }
    pub fn route(&self, pid: u64, irq: u64) -> Result<(), IoError> {
        unsafe { hunde_driver_route(self.storage.as_ptr(), pid, irq) }.into_result()
    }
    pub fn route_self(&self, irq: u64) -> Result<(), IoError> {
        unsafe { hunde_driver_route_self(self.storage.as_ptr(), irq) }.into_result()
    }
    pub fn rearm(&self, irq: u64) -> Result<(), IoError> {
        unsafe { hunde_driver_rearm(self.storage.as_ptr(), irq) }.into_result()
    }
    pub fn poll(&self) -> Result<Option<Event>, IoError> {
        let event = unsafe { hunde_driver_event(self.storage.as_ptr()) };
        if event.status.kind == crate::abi::PENDING {
            return Ok(None);
        }
        event.status.into_result()?;
        Ok(Some(Event::new(
            event.irq,
            event.sequence,
            event.kernel_at_usec,
            event.manager_at_usec,
        )))
    }
}

pub struct Listener {
    storage: Storage,
}

impl Listener {
    pub fn publish_ready(&self, name: &[u8]) -> Result<(), IoError> {
        unsafe { crate::abi::host::hunde_rust_publish_ready(ByteSpan::new(name)) }.into_result()
    }
    pub fn listen(name: &[u8]) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { hunde_driver_listener_layout() })?;
        unsafe { hunde_driver_listen(storage.as_ptr(), ByteSpan::new(name)) }.into_result()?;
        Ok(Self { storage })
    }
    pub fn connections(&self) -> impl Iterator<Item = u64> + '_ {
        (0..)
            .map_while(|index| {
                let result = unsafe { hunde_driver_arrival(self.storage.as_ptr(), index) };
                (result.status.kind == crate::abi::SUCCESS).then_some(result.value)
            })
            .filter(|connection| *connection != 0)
    }
}

pub struct Client {
    storage: Storage,
    connection: u64,
}

impl Client {
    pub fn accept(connection: u64) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { hunde_driver_client_layout() })?;
        unsafe { hunde_driver_accept(storage.as_ptr(), connection) }.into_result()?;
        Ok(Self {
            storage,
            connection,
        })
    }
    pub fn connection(&self) -> u64 {
        self.connection
    }
    pub fn pid(&self) -> u64 {
        unsafe { hunde_driver_client_pid(self.storage.as_ptr()) }
    }
    pub fn poll(&mut self) -> Result<Option<Request>, IoError> {
        unsafe { hunde_driver_request(self.storage.as_ptr()) }.into_result()
    }
    pub fn reply(&mut self, response: Response) -> Result<bool, IoError> {
        let result = unsafe {
            hunde_driver_reply(
                self.storage.as_ptr(),
                response.kind,
                response.code,
                response.irq,
                response.generation,
                response.sequence,
                response.kernel_at_usec,
                response.manager_at_usec,
            )
        };
        if result.kind == crate::abi::PENDING {
            return Ok(false);
        }
        result.into_result()?;
        Ok(true)
    }
}

pub fn prepare_wait() {
    unsafe { crate::abi::host::hunde_rust_channel_prepare_wait() };
}
pub fn wait() {
    unsafe { crate::abi::host::hunde_rust_channel_wait() };
}

pub fn wait_ticks(ticks: u64) {
    unsafe { crate::abi::host::hunde_rust_channel_wait_ticks(ticks) };
}

const _: () = {
    assert!(core::mem::size_of::<NativeValue>() == 40);
    assert!(core::mem::size_of::<NativeEvent>() == 64);
    assert!(core::mem::size_of::<NativeRequest>() == 64);
};

unsafe extern "C" {
    fn hunde_driver_manager_layout() -> NativeLayout;
    fn hunde_driver_manager_init(storage: *mut c_void) -> StatusResult;
    fn hunde_driver_route(storage: *mut c_void, pid: u64, irq: u64) -> StatusResult;
    fn hunde_driver_route_self(storage: *mut c_void, irq: u64) -> StatusResult;
    fn hunde_driver_rearm(storage: *mut c_void, irq: u64) -> StatusResult;
    fn hunde_driver_event(storage: *mut c_void) -> NativeEvent;
    fn hunde_driver_listener_layout() -> NativeLayout;
    fn hunde_driver_listen(storage: *mut c_void, name: ByteSpan) -> StatusResult;
    fn hunde_driver_arrival(storage: *mut c_void, index: u64) -> NativeValue;
    fn hunde_driver_client_layout() -> NativeLayout;
    fn hunde_driver_accept(storage: *mut c_void, connection: u64) -> StatusResult;
    fn hunde_driver_client_pid(storage: *mut c_void) -> u64;
    fn hunde_driver_request(storage: *mut c_void) -> NativeRequest;
    fn hunde_driver_reply(
        storage: *mut c_void,
        kind: u32,
        code: i32,
        irq: u64,
        generation: u64,
        sequence: u64,
        kernel_at_usec: u64,
        manager_at_usec: u64,
    ) -> StatusResult;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn requests_translate_wire_operations_without_accepting_unknown_values() {
        for (kind, operation) in [
            (INTERRUPT_PROTOCOL_SUBSCRIBE, Operation::Subscribe),
            (INTERRUPT_PROTOCOL_WAIT, Operation::Wait),
            (INTERRUPT_PROTOCOL_ACKNOWLEDGE, Operation::Acknowledge),
            (99, Operation::Invalid),
        ] {
            let raw = NativeRequest {
                status: StatusResult::success(),
                kind,
                reserved: 0,
                irq: 48,
                generation: 2,
                sequence: 7,
            };
            assert_eq!(
                raw.into_result(),
                Ok(Some(Request::new(operation, 48, 2, 7)))
            );
        }
    }
}

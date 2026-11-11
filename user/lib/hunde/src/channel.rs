use crate::abi::{ByteSpan, IoError, NativeValue, StatusResult, host::NativeLayout};
use crate::native::Storage;
use core::ffi::c_void;

pub struct Listener {
    storage: Storage,
}

impl Listener {
    pub fn publish_ready(&self, name: &[u8]) -> Result<(), IoError> {
        unsafe { crate::abi::host::hunde_rust_publish_ready(ByteSpan::new(name)) }.into_result()
    }
    pub fn listen(name: &[u8]) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { hunde_channel_listener_layout() })?;
        unsafe { hunde_channel_listen(storage.as_ptr(), ByteSpan::new(name)) }.into_result()?;
        Ok(Self { storage })
    }
    pub fn connections(&self) -> impl Iterator<Item = u64> + '_ {
        (0..)
            .map_while(|index| {
                let result = unsafe { hunde_channel_arrival(self.storage.as_ptr(), index) };
                (result.status.kind == crate::abi::SUCCESS).then_some(result.value)
            })
            .filter(|connection| *connection != 0)
    }
}

fn pending(status: StatusResult) -> Result<bool, IoError> {
    if status.kind == crate::abi::PENDING {
        return Ok(false);
    }
    status.into_result()?;
    Ok(true)
}

pub struct Client {
    storage: Storage,
    open: bool,
}

impl Client {
    pub fn open(endpoint: &[u8]) -> Result<Self, IoError> {
        if endpoint.is_empty() || endpoint.len() > 255 || endpoint.contains(&0) {
            return Err(IoError(crate::protocol::IO_INVALID_PATH));
        }
        let storage = Storage::new(unsafe { hunde_channel_client_layout() })?;
        unsafe { hunde_channel_open(storage.as_ptr(), ByteSpan::new(endpoint)) }.into_result()?;
        Ok(Self {
            storage,
            open: true,
        })
    }

    pub fn peer_pid(&self) -> u64 {
        unsafe { hunde_channel_server_pid(self.storage.as_ptr()) }
    }

    pub fn send(&mut self, bytes: &[u8]) -> Result<bool, IoError> {
        if !self.open {
            return Err(IoError(crate::protocol::IO_NOT_OPEN));
        }
        pending(unsafe { hunde_channel_send(self.storage.as_ptr(), ByteSpan::new(bytes)) })
    }

    pub fn receive(&mut self, target: &mut [u8]) -> Result<Option<usize>, IoError> {
        if !self.open {
            return Err(IoError(crate::protocol::IO_NOT_OPEN));
        }
        let count = unsafe {
            hunde_channel_receive(
                self.storage.as_ptr(),
                target.as_mut_ptr(),
                target.len() as u64,
            )
        }
        .optional()?;
        checked_count(count, target.len())
    }

    pub fn exchange(&mut self, request: &[u8], reply: &mut [u8]) -> Result<usize, IoError> {
        crate::driver::prepare_wait();
        while !self.send(request)? {
            crate::driver::wait();
        }
        loop {
            if let Some(count) = self.receive(reply)? {
                return Ok(count);
            }
            crate::driver::wait();
        }
    }

    pub fn close(&mut self) {
        if self.open {
            unsafe { hunde_channel_close(self.storage.as_ptr()) };
            self.open = false;
        }
    }
}

impl Drop for Client {
    fn drop(&mut self) {
        self.close();
    }
}

pub struct Server {
    storage: Storage,
    connection: u64,
    open: bool,
}

impl Server {
    pub fn accept(connection: u64) -> Result<Self, IoError> {
        let storage = Storage::new(unsafe { hunde_channel_server_layout() })?;
        unsafe { hunde_channel_accept(storage.as_ptr(), connection) }.into_result()?;
        Ok(Self {
            storage,
            connection,
            open: true,
        })
    }

    pub fn connection(&self) -> u64 {
        self.connection
    }

    pub fn peer_pid(&self) -> u64 {
        unsafe { hunde_channel_transport_pid(self.storage.as_ptr()) }
    }

    pub fn alive(&self) -> bool {
        self.open && unsafe { hunde_channel_server_alive(self.storage.as_ptr()) != 0 }
    }

    pub fn receive(&mut self, target: &mut [u8]) -> Result<Option<usize>, IoError> {
        let count = unsafe {
            hunde_channel_server_read(
                self.storage.as_ptr(),
                target.as_mut_ptr(),
                target.len() as u64,
            )
        }
        .optional()?;
        checked_count(count, target.len())
    }

    pub fn reply(&mut self, bytes: &[u8]) -> Result<bool, IoError> {
        pending(unsafe { hunde_channel_server_reply(self.storage.as_ptr(), ByteSpan::new(bytes)) })
    }

    pub fn close(&mut self) {
        if self.open {
            unsafe { hunde_channel_server_close(self.storage.as_ptr()) };
            self.open = false;
        }
    }
}

impl Drop for Server {
    fn drop(&mut self) {
        self.close();
    }
}

fn checked_count(count: Option<u64>, capacity: usize) -> Result<Option<usize>, IoError> {
    match count {
        Some(count) if count > capacity as u64 => Err(IoError(crate::protocol::IO_UNKNOWN_BUFFER)),
        count => Ok(count.map(|count| count as usize)),
    }
}

unsafe extern "C" {
    fn hunde_channel_listener_layout() -> NativeLayout;
    fn hunde_channel_listen(storage: *mut c_void, name: ByteSpan) -> StatusResult;
    fn hunde_channel_arrival(storage: *mut c_void, index: u64) -> NativeValue;
    fn hunde_channel_client_layout() -> NativeLayout;
    fn hunde_channel_open(storage: *mut c_void, endpoint: ByteSpan) -> StatusResult;
    fn hunde_channel_send(storage: *mut c_void, request: ByteSpan) -> StatusResult;
    fn hunde_channel_receive(storage: *mut c_void, target: *mut u8, capacity: u64) -> NativeValue;
    fn hunde_channel_server_pid(storage: *mut c_void) -> u64;
    fn hunde_channel_close(storage: *mut c_void);
    fn hunde_channel_server_layout() -> NativeLayout;
    fn hunde_channel_accept(storage: *mut c_void, connection: u64) -> StatusResult;
    fn hunde_channel_server_read(
        storage: *mut c_void,
        target: *mut u8,
        capacity: u64,
    ) -> NativeValue;
    fn hunde_channel_server_reply(storage: *mut c_void, reply: ByteSpan) -> StatusResult;
    fn hunde_channel_transport_pid(storage: *mut c_void) -> u64;
    fn hunde_channel_server_alive(storage: *mut c_void) -> u32;
    fn hunde_channel_server_close(storage: *mut c_void);
}

const _: () = assert!(core::mem::size_of::<NativeValue>() == 40);

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn counts_are_bounded_and_pending_is_not_success() {
        assert_eq!(checked_count(None, 4), Ok(None));
        assert_eq!(checked_count(Some(4), 4), Ok(Some(4)));
        assert_eq!(
            checked_count(Some(5), 4),
            Err(IoError(crate::protocol::IO_UNKNOWN_BUFFER))
        );
        assert_eq!(pending(StatusResult::success()), Ok(true));
        let mut status = StatusResult::success();
        status.kind = crate::abi::PENDING;
        assert_eq!(pending(status), Ok(false));
        assert_eq!(
            pending(StatusResult::from_result(Err(IoError(-17)))),
            Err(IoError(-17))
        );
    }
}

mod client;
mod server;
mod wire;

pub use client::Client;
pub use server::Server;

use crate::abi::IoError;
use crate::protocol::*;
use alloc::vec::Vec;

pub const FRAME_CAPACITY: usize = PACKET_FRAME_CAPACITY as usize;
pub const QUEUE_CAPACITY: usize = PACKET_QUEUE_CAPACITY as usize;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Error {
    Invalid,
    Busy,
    Closed,
    LinkDown,
    Io,
    Denied,
    Native(IoError),
}

impl Error {
    pub fn into_io(self) -> IoError {
        match self {
            Self::Native(error) => error,
            Self::Busy => IoError(IO_WOULD_BLOCK),
            Self::Denied => IoError(IO_PERMISSION_DENIED),
            Self::Closed => IoError(IO_NOT_OPEN),
            _ => IoError(IO_NO_DEVICE_FOUND),
        }
    }
}

impl From<IoError> for Error {
    fn from(error: IoError) -> Self {
        Self::Native(error)
    }
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub struct Info {
    mac: [u8; 6],
    mtu: usize,
    link_up: bool,
}

impl Info {
    pub fn new(mac: [u8; 6], mtu: usize, link_up: bool) -> Result<Self, Error> {
        if mac == [0; 6]
            || mac[0] & 1 != 0
            || mtu == 0
            || mtu > FRAME_CAPACITY - PACKET_ETHERNET_HEADER_SIZE as usize
        {
            return Err(Error::Invalid);
        }
        Ok(Self { mac, mtu, link_up })
    }

    pub fn mac(self) -> [u8; 6] {
        self.mac
    }
    pub fn mtu(self) -> usize {
        self.mtu
    }
    pub fn link_up(self) -> bool {
        self.link_up
    }
    pub fn frame_limit(self) -> usize {
        self.mtu + PACKET_ETHERNET_HEADER_SIZE as usize
    }
}

#[derive(Clone, Debug, PartialEq, Eq)]
pub struct Frame(Vec<u8>);

impl Frame {
    pub fn new(bytes: &[u8]) -> Result<Self, Error> {
        if !(PACKET_ETHERNET_HEADER_SIZE as usize..=FRAME_CAPACITY).contains(&bytes.len()) {
            return Err(Error::Invalid);
        }
        Self::from_vec(bytes.to_vec())
    }

    pub fn from_vec(bytes: Vec<u8>) -> Result<Self, Error> {
        if !(PACKET_ETHERNET_HEADER_SIZE as usize..=FRAME_CAPACITY).contains(&bytes.len()) {
            return Err(Error::Invalid);
        }
        Ok(Self(bytes))
    }

    pub fn as_bytes(&self) -> &[u8] {
        &self.0
    }
    pub fn len(&self) -> usize {
        self.0.len()
    }
    pub fn is_empty(&self) -> bool {
        self.0.is_empty()
    }
}

pub trait Device {
    fn info(&self) -> Info;
    fn transmit(&mut self, frame: Frame) -> Result<(), Frame>;
    fn receive(&mut self) -> Result<Option<Frame>, Error>;
    fn clear(&mut self);
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum Direction {
    Rx,
    Tx,
}

impl Direction {
    fn buffer(self) -> u32 {
        match self {
            Self::Rx => BUFFER_READ,
            Self::Tx => BUFFER_WRITE,
        }
    }
}

struct Mapping {
    buffer: wire::Buffer,
    peer: u64,
}

impl Mapping {
    fn attach(buffer: wire::Buffer, peer: u64, direction: Direction) -> Result<Self, Error> {
        if buffer.direction != direction.buffer() {
            return Err(Error::Invalid);
        }
        let mapping = Self { buffer, peer };
        mapping.validate(0, 0)?;
        Ok(mapping)
    }

    fn validate(&self, offset: u64, length: u64) -> Result<usize, Error> {
        if offset > self.buffer.capacity || length > self.buffer.capacity - offset {
            return Err(Error::Invalid);
        }
        unsafe { hunde_buffer_validate(&self.buffer, self.peer, self.buffer.direction) }
            .into_result()?;
        let address = self
            .buffer
            .address
            .checked_add(offset)
            .ok_or(Error::Invalid)?;
        usize::try_from(address).map_err(|_| Error::Invalid)
    }

    fn read(&self, offset: u64, target: &mut [u8]) -> Result<(), Error> {
        let address = self.validate(offset, target.len() as u64)?;
        unsafe { crate::native::copy_from_mapped(address as *const u8, target) };
        Ok(())
    }

    fn write(&self, offset: u64, source: &[u8]) -> Result<(), Error> {
        let address = self.validate(offset, source.len() as u64)?;
        unsafe { crate::native::copy_to_mapped(address as *mut u8, source) };
        Ok(())
    }
}

unsafe extern "C" {
    fn hunde_buffer_validate(
        buffer: *const wire::Buffer,
        peer: u64,
        direction: u32,
    ) -> crate::abi::StatusResult;
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn interface_capabilities_and_frames_have_explicit_bounds() {
        assert_eq!(Info::new([0; 6], 1500, true), Err(Error::Invalid));
        assert_eq!(
            Info::new([1, 0, 0, 0, 0, 1], 1500, true),
            Err(Error::Invalid)
        );
        assert_eq!(
            Info::new([2, 0, 0, 0, 0, 1], 1501, true),
            Err(Error::Invalid)
        );
        assert_eq!(Frame::new(&[0; 13]), Err(Error::Invalid));
        assert_eq!(Frame::new(&[0; FRAME_CAPACITY + 1]), Err(Error::Invalid));
        assert_eq!(
            Frame::new(&[255; FRAME_CAPACITY]).unwrap().len(),
            FRAME_CAPACITY
        );
    }
}

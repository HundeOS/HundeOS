use super::{Direction, Error, Info};
use crate::protocol::*;
use core::mem::size_of;

#[repr(C)]
#[derive(Clone, Copy, Debug, Default, PartialEq, Eq)]
pub(super) struct Buffer {
    pub(super) id: u64,
    pub(super) address: u64,
    pub(super) capacity: u64,
    pub(super) direction: u32,
    pub(super) reserved: u32,
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub(super) struct Request {
    pub(super) version: u32,
    pub(super) operation: u32,
    pub(super) buffer: u64,
    pub(super) offset: u64,
    pub(super) length: u64,
}

impl Request {
    pub(super) fn new(operation: u32, buffer: u64, offset: u64, length: u64) -> Self {
        Self {
            version: PACKET_PROTOCOL_VERSION,
            operation,
            buffer,
            offset,
            length,
        }
    }

    pub(super) fn valid(&self) -> bool {
        self.version == PACKET_PROTOCOL_VERSION
            && match self.operation {
                PACKET_OPEN_RX | PACKET_OPEN_TX | PACKET_CLOSE => {
                    self.buffer == 0 && self.offset == 0 && self.length == 0
                }
                PACKET_TRANSMIT | PACKET_RECEIVE => self.buffer != 0,
                _ => false,
            }
    }

    pub(super) fn transfer_is_valid(
        &self,
        buffer: Buffer,
        direction: Direction,
        info: Info,
    ) -> bool {
        self.valid()
            && self.operation
                == match direction {
                    Direction::Tx => PACKET_TRANSMIT,
                    Direction::Rx => PACKET_RECEIVE,
                }
            && self.buffer == buffer.id
            && self.offset <= buffer.capacity
            && self.length <= buffer.capacity - self.offset
            && (PACKET_ETHERNET_HEADER_SIZE..=info.frame_limit() as u64).contains(&self.length)
            && (direction != Direction::Rx || self.length == info.frame_limit() as u64)
    }
}

#[repr(C)]
#[derive(Clone, Copy, Debug)]
pub(super) struct Response {
    pub(super) version: u32,
    pub(super) status: i32,
    pub(super) length: u64,
    pub(super) buffer: Buffer,
    pub(super) mtu: u32,
    pub(super) link_up: u32,
    pub(super) mac: [u8; 6],
    pub(super) reserved: [u8; 2],
}

impl Response {
    pub(super) fn new(status: i32, info: Info) -> Self {
        Self {
            version: PACKET_PROTOCOL_VERSION,
            status,
            length: 0,
            buffer: Buffer::default(),
            mtu: info.mtu() as u32,
            link_up: u32::from(info.link_up()),
            mac: info.mac(),
            reserved: [0; 2],
        }
    }

    pub(super) fn info(&self) -> Result<Info, Error> {
        if self.version != PACKET_PROTOCOL_VERSION || self.reserved != [0; 2] || self.link_up > 1 {
            return Err(Error::Invalid);
        }
        Info::new(self.mac, self.mtu as usize, self.link_up != 0)
    }

    pub(super) fn result(&self) -> Result<(), Error> {
        self.info()?;
        match self.status {
            PACKET_OK => Ok(()),
            PACKET_INVALID => Err(Error::Invalid),
            PACKET_BUSY => Err(Error::Busy),
            PACKET_CLOSED => Err(Error::Closed),
            PACKET_LINK_DOWN => Err(Error::LinkDown),
            PACKET_IO_ERROR => Err(Error::Io),
            PACKET_DENIED => Err(Error::Denied),
            _ => Err(Error::Invalid),
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(super) struct BufferRequest {
    service_length: u64,
    service_data: [u8; 256],
    operation: u32,
    direction: u32,
    client: u64,
    id: u64,
    second: u64,
}

impl BufferRequest {
    pub(super) fn acquire(service: &[u8], client: u64, direction: u32) -> Self {
        let mut request = Self {
            service_length: service.len() as u64,
            service_data: [0; 256],
            operation: BUFFER_ACQUIRE,
            direction,
            client,
            id: 0,
            second: 0,
        };
        request.service_data[..service.len()].copy_from_slice(service);
        request
    }

    pub(super) fn release(buffer: Buffer) -> Self {
        Self {
            service_length: 0,
            service_data: [0; 256],
            operation: BUFFER_RELEASE,
            direction: 0,
            client: 0,
            id: buffer.id,
            second: 0,
        }
    }
}

#[repr(C)]
#[derive(Clone, Copy)]
pub(super) struct BufferReply {
    pub(super) status: i32,
    reserved: u32,
    pub(super) buffer: Buffer,
}

/// Records have no padding or invalid bit patterns and may be copied as bytes.
///
/// # Safety
/// Every byte must be initialized, and every possible byte pattern must be valid.
pub(super) unsafe trait Record: Copy {}

unsafe impl Record for Request {}
unsafe impl Record for Response {}
unsafe impl Record for BufferRequest {}
unsafe impl Record for BufferReply {}

pub(super) fn bytes<Value: Record>(value: &Value) -> &[u8] {
    unsafe { core::slice::from_raw_parts((value as *const Value).cast(), size_of::<Value>()) }
}

pub(super) fn decode<Value: Record>(bytes: &[u8]) -> Result<Value, Error> {
    if bytes.len() != size_of::<Value>() {
        return Err(Error::Invalid);
    }
    Ok(unsafe { bytes.as_ptr().cast::<Value>().read_unaligned() })
}

const _: () = {
    assert!(size_of::<Buffer>() == 32);
    assert!(size_of::<Request>() == 32);
    assert!(size_of::<Response>() == 64);
    assert!(size_of::<BufferRequest>() == 296);
    assert!(size_of::<BufferReply>() == 40);
};

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn records_round_trip_without_truncation_or_accepting_unknown_operations() {
        let request = Request::new(PACKET_TRANSMIT, 543, 27, 1514);
        let decoded: Request = decode(bytes(&request)).unwrap();
        assert_eq!(
            (decoded.buffer, decoded.offset, decoded.length),
            (543, 27, 1514)
        );
        assert!(decoded.valid());
        assert!(decode::<Request>(&bytes(&request)[1..]).is_err());
        assert!(!Request::new(9999, 0, 0, 0).valid());
        assert!(!Request::new(PACKET_OPEN_RX, 8, 0, 0).valid());
        let mut old = request;
        old.version += 1;
        assert!(!old.valid());
    }

    #[test]
    fn response_tags_lengths_and_metadata_are_checked() {
        let info = Info::new([2, 0, 0, 0, 0, 1], 1500, true).unwrap();
        let mut reply = Response::new(PACKET_OK, info);
        assert_eq!(reply.result(), Ok(()));
        assert_eq!(decode::<Response>(bytes(&reply)).unwrap().info(), Ok(info));
        reply.status = -9999;
        assert_eq!(reply.result(), Err(Error::Invalid));
        reply.status = PACKET_DENIED;
        assert_eq!(reply.result(), Err(Error::Denied));
        reply.link_up = 2;
        assert_eq!(reply.info(), Err(Error::Invalid));
    }

    #[test]
    fn transfer_checks_identity_direction_extent_and_receive_capacity() {
        let info = Info::new([2, 0, 0, 0, 0, 1], 1500, true).unwrap();
        let buffer = Buffer {
            id: 543,
            address: 0,
            capacity: 4096,
            direction: BUFFER_WRITE,
            reserved: 0,
        };
        assert!(
            Request::new(PACKET_TRANSMIT, 543, 2582, 1514).transfer_is_valid(
                buffer,
                Direction::Tx,
                info
            )
        );
        for request in [
            Request::new(PACKET_TRANSMIT, 544, 0, 64),
            Request::new(PACKET_TRANSMIT, 543, 2583, 1514),
            Request::new(PACKET_TRANSMIT, 543, u64::MAX, 64),
            Request::new(PACKET_TRANSMIT, 543, 0, 13),
            Request::new(PACKET_TRANSMIT, 543, 0, 1515),
            Request::new(PACKET_RECEIVE, 543, 0, 1514),
            Request::new(PACKET_CLOSE, 0, 0, 0),
        ] {
            assert!(!request.transfer_is_valid(buffer, Direction::Tx, info));
        }
        assert!(
            Request::new(PACKET_RECEIVE, 543, 0, 1514).transfer_is_valid(
                buffer,
                Direction::Rx,
                info
            )
        );
        assert!(!Request::new(PACKET_RECEIVE, 543, 0, 64).transfer_is_valid(
            buffer,
            Direction::Rx,
            info
        ));
    }
}
